#include "device_registry.h"

#include <stdlib.h>
#include <string.h>

struct gw_registry {
    gw_device_t devices[GW_MAX_DEVICES];
    size_t count;
    uint16_t next_device_id;
};

static int find_index(const struct gw_registry *reg, const uint8_t mac[GW_MAC_LEN]) {
    for (int i = 0; i < GW_MAX_DEVICES; i++) {
        if (reg->devices[i].in_use && memcmp(reg->devices[i].mac, mac, GW_MAC_LEN) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_free_index(const struct gw_registry *reg) {
    for (int i = 0; i < GW_MAX_DEVICES; i++) {
        if (!reg->devices[i].in_use) return i;
    }
    return -1;
}

gw_registry_t *gw_registry_create(void) {
    return calloc(1, sizeof(struct gw_registry));
}

void gw_registry_destroy(gw_registry_t *reg) {
    free(reg);
}

gw_device_t *gw_registry_touch(gw_registry_t *reg, const uint8_t mac[GW_MAC_LEN],
                                const char *hostname, uint64_t now_ms) {
    if (!reg) return NULL;

    int idx = find_index(reg, mac);
    if (idx < 0) {
        idx = find_free_index(reg);
        if (idx < 0) return NULL; /* table full */
        gw_device_t *dev = &reg->devices[idx];
        memset(dev, 0, sizeof(*dev));
        dev->in_use = true;
        memcpy(dev->mac, mac, GW_MAC_LEN);
        dev->device_id = reg->next_device_id++;
        dev->state = GW_STATE_UNKNOWN;
        dev->last_state_change_ms = now_ms;
        reg->count++;
    }

    gw_device_t *dev = &reg->devices[idx];
    if (hostname) {
        size_t hlen = strlen(hostname);
        if (hlen > GW_HOSTNAME_MAX) hlen = GW_HOSTNAME_MAX;
        memcpy(dev->hostname, hostname, hlen);
        dev->hostname[hlen] = '\0';
    }
    dev->last_seen_ms = now_ms;
    return dev;
}

gw_device_t *gw_registry_find(gw_registry_t *reg, const uint8_t mac[GW_MAC_LEN]) {
    if (!reg) return NULL;
    int idx = find_index(reg, mac);
    return idx < 0 ? NULL : &reg->devices[idx];
}

bool gw_registry_remove(gw_registry_t *reg, const uint8_t mac[GW_MAC_LEN]) {
    if (!reg) return false;
    int idx = find_index(reg, mac);
    if (idx < 0) return false;
    reg->devices[idx].in_use = false;
    reg->count--;
    return true;
}

bool gw_registry_apply_event(gw_registry_t *reg, const uint8_t mac[GW_MAC_LEN],
                              gw_event_t event, uint64_t now_ms) {
    if (!reg) return false;
    int idx = find_index(reg, mac);
    if (idx < 0) return false;

    gw_device_t *dev = &reg->devices[idx];
    if (!gw_sm_handle_event(&dev->state, event)) return false;

    dev->last_state_change_ms = now_ms;
    if (event == GW_EVT_HEARTBEAT_OK) dev->last_seen_ms = now_ms;
    return true;
}

int gw_registry_sweep_stale(gw_registry_t *reg, uint64_t now_ms, uint32_t timeout_ms) {
    if (!reg) return 0;
    int evicted = 0;

    for (int i = 0; i < GW_MAX_DEVICES; i++) {
        gw_device_t *dev = &reg->devices[i];
        if (!dev->in_use) continue;
        if (dev->state != GW_STATE_CONNECTED &&
            dev->state != GW_STATE_DEGRADED &&
            dev->state != GW_STATE_RECONNECTING) {
            continue;
        }

        /* CONNECTED devices are timed against their last real heartbeat.
         * DEGRADED/RECONNECTING devices are, by definition, not receiving
         * heartbeats any more -- pacing further escalation off the stale
         * last_seen_ms would re-fire on every sweep call instead of once
         * per timeout window, so those use last_state_change_ms instead. */
        uint64_t reference = (dev->state == GW_STATE_CONNECTED)
                                  ? dev->last_seen_ms
                                  : dev->last_state_change_ms;
        if (now_ms - reference <= timeout_ms) continue;

        if (gw_sm_handle_event(&dev->state, GW_EVT_HEARTBEAT_TIMEOUT)) {
            dev->last_state_change_ms = now_ms;
            if (dev->state == GW_STATE_OFFLINE) {
                dev->in_use = false;
                reg->count--;
                evicted++;
            }
        }
    }
    return evicted;
}

size_t gw_registry_count(const gw_registry_t *reg) {
    return reg ? reg->count : 0;
}

/* Fixed-size record: mac(6) + hostname_len(1) + hostname(31) + device_id(2) + state(1) = 41 bytes */
#define SER_RECORD_SIZE  (GW_MAC_LEN + 1 + GW_HOSTNAME_MAX + 2 + 1)

size_t gw_registry_serialize(const gw_registry_t *reg, void *buf, size_t buf_cap) {
    if (!reg || !buf) return 0;

    uint32_t count = (uint32_t)reg->count;
    size_t needed = sizeof(count) + (size_t)count * SER_RECORD_SIZE;
    if (needed > buf_cap) return 0;

    uint8_t *p = (uint8_t *)buf;
    memcpy(p, &count, sizeof(count));
    p += sizeof(count);

    for (int i = 0; i < GW_MAX_DEVICES; i++) {
        const gw_device_t *dev = &reg->devices[i];
        if (!dev->in_use) continue;

        memcpy(p, dev->mac, GW_MAC_LEN); p += GW_MAC_LEN;
        uint8_t hlen = (uint8_t)strlen(dev->hostname);
        *p++ = hlen;
        memset(p, 0, GW_HOSTNAME_MAX);
        memcpy(p, dev->hostname, hlen);
        p += GW_HOSTNAME_MAX;
        memcpy(p, &dev->device_id, sizeof(dev->device_id)); p += sizeof(dev->device_id);
        *p++ = (uint8_t)dev->state;
    }
    return (size_t)(p - (uint8_t *)buf);
}

size_t gw_registry_deserialize(gw_registry_t *reg, const void *buf, size_t len, uint64_t now_ms) {
    if (!reg || !buf || len < sizeof(uint32_t)) return 0;

    const uint8_t *p = (const uint8_t *)buf;
    uint32_t count;
    memcpy(&count, p, sizeof(count));
    p += sizeof(count);

    size_t expected = sizeof(count) + (size_t)count * SER_RECORD_SIZE;
    if (expected > len) return 0;

    size_t restored = 0;
    for (uint32_t i = 0; i < count; i++) {
        int idx = find_free_index(reg);
        if (idx < 0) break; /* table already full; drop remainder */

        gw_device_t *dev = &reg->devices[idx];
        memset(dev, 0, sizeof(*dev));
        dev->in_use = true;

        memcpy(dev->mac, p, GW_MAC_LEN); p += GW_MAC_LEN;
        uint8_t hlen = *p++;
        if (hlen > GW_HOSTNAME_MAX) hlen = GW_HOSTNAME_MAX;
        memcpy(dev->hostname, p, hlen);
        dev->hostname[hlen] = '\0';
        p += GW_HOSTNAME_MAX;
        memcpy(&dev->device_id, p, sizeof(dev->device_id)); p += sizeof(dev->device_id);
        dev->state = (gw_state_t)(*p++);

        dev->last_seen_ms = now_ms;
        dev->last_state_change_ms = now_ms;
        reg->count++;
        restored++;
        if (dev->device_id >= reg->next_device_id) reg->next_device_id = (uint16_t)(dev->device_id + 1);
    }
    return restored;
}
