#ifndef GW_DEVICE_REGISTRY_H
#define GW_DEVICE_REGISTRY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "state_machine.h"

/* Client device table, analogous to a router's DHCP lease / ARP table.
 * Backed by a fixed-size array (GW_MAX_DEVICES) allocated once at
 * gw_registry_create() -- no per-device heap churn at runtime, which
 * matters on a target where fragmentation is a real failure mode, not
 * just a performance concern. */

#define GW_MAX_DEVICES      64
#define GW_MAC_LEN          6
#define GW_HOSTNAME_MAX     31

typedef struct {
    bool        in_use;
    uint8_t     mac[GW_MAC_LEN];
    char        hostname[GW_HOSTNAME_MAX + 1];
    uint16_t    device_id;
    uint64_t    last_seen_ms;          /* updated on HEARTBEAT_OK */
    uint64_t    last_state_change_ms;  /* updated on any successful transition */
    gw_state_t  state;
} gw_device_t;

typedef struct gw_registry gw_registry_t;

gw_registry_t *gw_registry_create(void);
void gw_registry_destroy(gw_registry_t *reg);

/* Finds an existing entry by MAC and refreshes its hostname/last_seen, or
 * allocates a new entry (state = GW_STATE_UNKNOWN) if there is a free
 * slot. Returns NULL if the table is full. */
gw_device_t *gw_registry_touch(gw_registry_t *reg, const uint8_t mac[GW_MAC_LEN],
                                const char *hostname, uint64_t now_ms);

gw_device_t *gw_registry_find(gw_registry_t *reg, const uint8_t mac[GW_MAC_LEN]);

bool gw_registry_remove(gw_registry_t *reg, const uint8_t mac[GW_MAC_LEN]);

/* Routes `event` through the state machine for the device identified by
 * `mac`. On success, updates last_state_change_ms (and last_seen_ms too,
 * for GW_EVT_HEARTBEAT_OK). Returns false if the device is unknown or the
 * transition was illegal. */
bool gw_registry_apply_event(gw_registry_t *reg, const uint8_t mac[GW_MAC_LEN],
                              gw_event_t event, uint64_t now_ms);

/* Escalates devices that have gone quiet: CONNECTED/DEGRADED devices are
 * timed against last_seen_ms, RECONNECTING devices against
 * last_state_change_ms (since by definition no heartbeats are arriving).
 * Devices that reach OFFLINE are removed from the table. Returns the
 * number of devices removed. */
int gw_registry_sweep_stale(gw_registry_t *reg, uint64_t now_ms, uint32_t timeout_ms);

size_t gw_registry_count(const gw_registry_t *reg);

/* Flattens all in-use entries into a fixed-record binary blob for NVS
 * persistence. Returns the number of bytes written, or 0 if buf_cap is
 * too small. */
size_t gw_registry_serialize(const gw_registry_t *reg, void *buf, size_t buf_cap);

/* Restores entries from a blob produced by gw_registry_serialize(). Used
 * on a warm boot to repopulate the table before any traffic has arrived;
 * `now_ms` is used as the restored last_seen_ms / last_state_change_ms
 * baseline so devices get one full timeout window to re-announce
 * themselves before being swept as stale. Returns the number of entries
 * restored. */
size_t gw_registry_deserialize(gw_registry_t *reg, const void *buf, size_t len, uint64_t now_ms);

#endif /* GW_DEVICE_REGISTRY_H */
