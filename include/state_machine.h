#ifndef GW_STATE_MACHINE_H
#define GW_STATE_MACHINE_H

#include <stdbool.h>

/* Connection lifecycle for a single client device attached to the gateway.
 * Mirrors the kind of explicit state machine used by real Wi-Fi/IoT
 * connectivity stacks: a device is never just "connected" or "not" -- it
 * passes through discovery and handshake, and a lost heartbeat degrades
 * the link gracefully (DEGRADED -> RECONNECTING) before the device is
 * dropped, rather than evicting it on a single missed beat. */
typedef enum {
    GW_STATE_UNKNOWN = 0,
    GW_STATE_DISCOVERING,
    GW_STATE_CONNECTED,
    GW_STATE_DEGRADED,
    GW_STATE_RECONNECTING,
    GW_STATE_OFFLINE,
    GW_STATE_COUNT
} gw_state_t;

typedef enum {
    GW_EVT_DISCOVERED = 0,
    GW_EVT_HANDSHAKE_OK,
    GW_EVT_HANDSHAKE_FAIL,
    GW_EVT_HEARTBEAT_OK,
    GW_EVT_HEARTBEAT_TIMEOUT,
    GW_EVT_MAX_RETRIES,
    GW_EVT_USER_DISCONNECT,
    GW_EVT_COUNT
} gw_event_t;

/* Applies `event` to `*state` per the fixed transition table. Returns true
 * and updates *state if the transition is legal for the current state;
 * returns false and leaves *state unchanged otherwise. Illegal transitions
 * are not an error condition for the caller -- e.g. a stray HEARTBEAT_OK
 * for a device that is still DISCOVERING is simply ignored. */
bool gw_sm_handle_event(gw_state_t *state, gw_event_t event);

const char *gw_sm_state_name(gw_state_t state);
const char *gw_sm_event_name(gw_event_t event);

#endif /* GW_STATE_MACHINE_H */
