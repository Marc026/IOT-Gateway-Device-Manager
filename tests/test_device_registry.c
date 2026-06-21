#include "device_registry.h"
#include "test.h"
#include <string.h>

static void make_mac(uint8_t mac[GW_MAC_LEN], uint8_t last_byte) {
    mac[0] = 0x02; mac[1] = 0; mac[2] = 0; mac[3] = 0; mac[4] = 0; mac[5] = last_byte;
}

int main(void) {
    gw_registry_t *reg = gw_registry_create();
    GW_ASSERT(reg != NULL);

    uint8_t mac1[GW_MAC_LEN]; make_mac(mac1, 1);

    gw_device_t *dev = gw_registry_touch(reg, mac1, "thermostat", 1000);
    GW_ASSERT(dev != NULL);
    GW_ASSERT_EQ(dev->state, GW_STATE_UNKNOWN);
    GW_ASSERT_STREQ(dev->hostname, "thermostat");
    GW_ASSERT_EQ(gw_registry_count(reg), (size_t)1);

    /* Drive it through discovery/handshake via apply_event. */
    GW_ASSERT(gw_registry_apply_event(reg, mac1, GW_EVT_DISCOVERED, 1000));
    GW_ASSERT(gw_registry_apply_event(reg, mac1, GW_EVT_HANDSHAKE_OK, 1100));
    dev = gw_registry_find(reg, mac1);
    GW_ASSERT_EQ(dev->state, GW_STATE_CONNECTED);

    /* Re-touching an existing MAC updates hostname/last_seen but does not
     * create a duplicate entry. */
    gw_registry_touch(reg, mac1, "thermostat-v2", 1200);
    GW_ASSERT_EQ(gw_registry_count(reg), (size_t)1);
    GW_ASSERT_STREQ(gw_registry_find(reg, mac1)->hostname, "thermostat-v2");

    /* apply_event on an unknown MAC fails cleanly. */
    uint8_t mac_ghost[GW_MAC_LEN]; make_mac(mac_ghost, 99);
    GW_ASSERT(!gw_registry_apply_event(reg, mac_ghost, GW_EVT_HEARTBEAT_OK, 1300));

    /* --- Stale escalation ladder, paced by SWEEP timeout windows --- */
    uint64_t t = 1200; /* last_seen_ms for mac1 after the touch above */
    uint32_t timeout_ms = 5000;

    /* Not yet stale. */
    GW_ASSERT_EQ(gw_registry_sweep_stale(reg, t + 1000, timeout_ms), 0);
    GW_ASSERT_EQ(gw_registry_find(reg, mac1)->state, GW_STATE_CONNECTED);

    /* First full timeout window with no heartbeat: CONNECTED -> DEGRADED. */
    t = t + timeout_ms + 1;
    GW_ASSERT_EQ(gw_registry_sweep_stale(reg, t, timeout_ms), 0); /* not evicted yet */
    GW_ASSERT_EQ(gw_registry_find(reg, mac1)->state, GW_STATE_DEGRADED);

    /* Re-sweeping immediately must NOT re-escalate before another full
     * timeout window has elapsed (paced by last_state_change_ms). */
    GW_ASSERT_EQ(gw_registry_sweep_stale(reg, t + 10, timeout_ms), 0);
    GW_ASSERT_EQ(gw_registry_find(reg, mac1)->state, GW_STATE_DEGRADED);

    /* Second full window: DEGRADED -> RECONNECTING. */
    t = t + timeout_ms + 1;
    GW_ASSERT_EQ(gw_registry_sweep_stale(reg, t, timeout_ms), 0);
    GW_ASSERT_EQ(gw_registry_find(reg, mac1)->state, GW_STATE_RECONNECTING);

    /* Third full window with still no handshake: RECONNECTING -> OFFLINE,
     * and the device is evicted from the table entirely. */
    t = t + timeout_ms + 1;
    GW_ASSERT_EQ(gw_registry_sweep_stale(reg, t, timeout_ms), 1);
    GW_ASSERT(gw_registry_find(reg, mac1) == NULL);
    GW_ASSERT_EQ(gw_registry_count(reg), (size_t)0);

    /* --- Capacity limit --- */
    gw_registry_t *full = gw_registry_create();
    for (int i = 0; i < GW_MAX_DEVICES; i++) {
        uint8_t mac[GW_MAC_LEN]; make_mac(mac, (uint8_t)i);
        GW_ASSERT(gw_registry_touch(full, mac, "x", 0) != NULL);
    }
    uint8_t overflow_mac[GW_MAC_LEN]; make_mac(overflow_mac, 200);
    GW_ASSERT(gw_registry_touch(full, overflow_mac, "overflow", 0) == NULL);
    GW_ASSERT_EQ(gw_registry_count(full), (size_t)GW_MAX_DEVICES);

    /* --- Explicit remove --- */
    uint8_t mac2[GW_MAC_LEN]; make_mac(mac2, 0);
    GW_ASSERT(gw_registry_remove(full, mac2));
    GW_ASSERT(gw_registry_find(full, mac2) == NULL);
    GW_ASSERT_EQ(gw_registry_count(full), (size_t)(GW_MAX_DEVICES - 1));
    GW_ASSERT(!gw_registry_remove(full, mac2)); /* already gone */

    /* --- Serialize / deserialize round trip (warm-boot persistence) --- */
    gw_registry_t *src = gw_registry_create();
    uint8_t macA[GW_MAC_LEN]; make_mac(macA, 10);
    uint8_t macB[GW_MAC_LEN]; make_mac(macB, 20);
    gw_registry_touch(src, macA, "router-port-1", 500);
    gw_registry_apply_event(src, macA, GW_EVT_DISCOVERED, 500);
    gw_registry_apply_event(src, macA, GW_EVT_HANDSHAKE_OK, 500);
    gw_registry_touch(src, macB, "router-port-2", 500);

    uint8_t blob[4096];
    size_t n = gw_registry_serialize(src, blob, sizeof(blob));
    GW_ASSERT(n > 0);

    gw_registry_t *dst = gw_registry_create();
    size_t restored = gw_registry_deserialize(dst, blob, n, 9000);
    GW_ASSERT_EQ(restored, (size_t)2);
    GW_ASSERT_EQ(gw_registry_count(dst), (size_t)2);

    gw_device_t *a = gw_registry_find(dst, macA);
    GW_ASSERT(a != NULL);
    GW_ASSERT_STREQ(a->hostname, "router-port-1");
    GW_ASSERT_EQ(a->state, GW_STATE_CONNECTED);
    GW_ASSERT_EQ(a->last_seen_ms, (uint64_t)9000); /* re-baselined to restore time */

    gw_registry_destroy(reg);
    gw_registry_destroy(full);
    gw_registry_destroy(src);
    gw_registry_destroy(dst);

    return GW_TEST_SUMMARY("device_registry");
}
