#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

#include "state_machine.h"
#include "device_registry.h"
#include "nvs_store.h"
#include "watchdog.h"
#include "gateway_protocol.h"

#define GATEWAY_PORT          9500
#define HEARTBEAT_TIMEOUT_MS  5000
#define SWEEP_INTERVAL_MS     1000
#define NVS_PATH              "gateway.nvs"
#define NVS_DEVTABLE_KEY      "devtable"

static volatile sig_atomic_t g_running = 1;
static void on_signal(int signo) { (void)signo; g_running = 0; }

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000ull);
}

static void on_net_wdt_fire(void *ctx) {
    (void)ctx;
    fprintf(stderr, "[watchdog] net_rx task missed its deadline -- flagging for restart\n");
    /* On real hardware this callback would tear down and recreate the
     * socket / radio driver state. The host UDP socket itself can't wedge
     * the way a hardware MAC/PHY can, so there's nothing further to do
     * here in simulation. */
}

static void persist_registry(const gw_registry_t *reg, gw_nvs_t *nvs) {
    uint8_t blob[4096];
    size_t n = gw_registry_serialize(reg, blob, sizeof(blob));
    if (n == 0 && gw_registry_count(reg) > 0) {
        fprintf(stderr, "[gw] warning: device table too large to persist this pass\n");
        return;
    }
    gw_nvs_set(nvs, NVS_DEVTABLE_KEY, blob, n);
}

int main(void) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    gw_nvs_t *nvs = gw_nvs_open(NVS_PATH);
    if (!nvs) { fprintf(stderr, "failed to open NVS store at %s\n", NVS_PATH); return 1; }

    gw_registry_t *reg = gw_registry_create();
    gw_wdt_t *wdt = gw_wdt_create();
    if (!reg || !wdt) { fprintf(stderr, "failed to allocate core state\n"); return 1; }

    /* Warm boot: restore whatever device table was persisted on the last
     * clean shutdown so devices don't all have to re-discover at once. */
    uint8_t blob[4096];
    size_t blob_len = 0;
    if (gw_nvs_get(nvs, NVS_DEVTABLE_KEY, blob, sizeof(blob), &blob_len) && blob_len > 0) {
        size_t restored = gw_registry_deserialize(reg, blob, blob_len, now_ms());
        if (restored > 0) printf("[gw] restored %zu device(s) from NVS\n", restored);
    }

    int net_wdt = gw_wdt_register(wdt, "net_rx", 3000, 3, on_net_wdt_fire, NULL, now_ms());

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(GATEWAY_PORT);
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(sock); return 1;
    }

    printf("gateway listening on UDP :%d (%zu device(s) restored)\n",
           GATEWAY_PORT, gw_registry_count(reg));

    uint64_t last_sweep = now_ms();

    while (g_running) {
        struct pollfd pfd;
        pfd.fd = sock;
        pfd.events = POLLIN;
        int pr = poll(&pfd, 1, 1000);
        uint64_t t = now_ms();

        if (pr > 0 && (pfd.revents & POLLIN)) {
            gw_packet_t pkt;
            struct sockaddr_in from;
            socklen_t fromlen = sizeof(from);
            ssize_t n = recvfrom(sock, &pkt, sizeof(pkt), 0, (struct sockaddr *)&from, &fromlen);

            if (n == (ssize_t)sizeof(pkt) && gw_proto_validate(&pkt)) {
                char host[32];
                size_t hlen = pkt.hostname_len < sizeof(host) - 1 ? pkt.hostname_len : sizeof(host) - 1;
                memcpy(host, pkt.hostname, hlen);
                host[hlen] = '\0';

                const gw_device_t *dev = gw_registry_touch(reg, pkt.mac, host, t);
                if (dev) {
                    switch (pkt.msg_type) {
                        case GW_MSG_DISCOVER:
                            gw_registry_apply_event(reg, pkt.mac, GW_EVT_DISCOVERED, t);
                            break;
                        case GW_MSG_HANDSHAKE:
                            gw_registry_apply_event(reg, pkt.mac, GW_EVT_HANDSHAKE_OK, t);
                            break;
                        case GW_MSG_HEARTBEAT:
                            gw_registry_apply_event(reg, pkt.mac, GW_EVT_HEARTBEAT_OK, t);
                            break;
                        case GW_MSG_BYE:
                            gw_registry_apply_event(reg, pkt.mac, GW_EVT_USER_DISCONNECT, t);
                            gw_registry_remove(reg, pkt.mac);
                            break;
                        default:
                            break;
                    }
                    printf("[gw] %-20s -> %-12s (id=%u, seq=%u)\n",
                           host, gw_sm_state_name(dev->state),
                           gw_proto_get_device_id(&pkt), gw_proto_get_seq(&pkt));
                }
            }
        } else if (pr < 0) {
            if (errno != EINTR) perror("poll");
        }

        gw_wdt_kick(wdt, net_wdt, t);

        if (t - last_sweep >= SWEEP_INTERVAL_MS) {
            int evicted = gw_registry_sweep_stale(reg, t, HEARTBEAT_TIMEOUT_MS);
            if (evicted > 0) printf("[gw] swept %d stale device(s)\n", evicted);
            last_sweep = t;
        }

        gw_wdt_check(wdt, t);
    }

    printf("\nshutting down, persisting %zu device(s) to NVS...\n", gw_registry_count(reg));
    persist_registry(reg, nvs);
    gw_nvs_compact(nvs);

    close(sock);
    gw_wdt_destroy(wdt);
    gw_registry_destroy(reg);
    gw_nvs_close(nvs);
    return 0;
}
