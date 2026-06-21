#include "state_machine.h"
#include <stddef.h>

typedef struct {
    gw_state_t from;
    gw_event_t evt;
    gw_state_t to;
} gw_transition_t;

static const gw_transition_t TRANSITIONS[] = {
    { GW_STATE_UNKNOWN,      GW_EVT_DISCOVERED,       GW_STATE_DISCOVERING  },
    { GW_STATE_DISCOVERING,  GW_EVT_HANDSHAKE_OK,     GW_STATE_CONNECTED    },
    { GW_STATE_DISCOVERING,  GW_EVT_HANDSHAKE_FAIL,   GW_STATE_OFFLINE      },
    { GW_STATE_CONNECTED,    GW_EVT_HEARTBEAT_OK,     GW_STATE_CONNECTED    },
    { GW_STATE_CONNECTED,    GW_EVT_HEARTBEAT_TIMEOUT,GW_STATE_DEGRADED     },
    { GW_STATE_CONNECTED,    GW_EVT_USER_DISCONNECT,  GW_STATE_OFFLINE      },
    { GW_STATE_DEGRADED,     GW_EVT_HEARTBEAT_OK,     GW_STATE_CONNECTED    },
    { GW_STATE_DEGRADED,     GW_EVT_HEARTBEAT_TIMEOUT,GW_STATE_RECONNECTING },
    { GW_STATE_DEGRADED,     GW_EVT_USER_DISCONNECT,  GW_STATE_OFFLINE      },
    { GW_STATE_RECONNECTING, GW_EVT_HANDSHAKE_OK,     GW_STATE_CONNECTED    },
    { GW_STATE_RECONNECTING, GW_EVT_HEARTBEAT_TIMEOUT,GW_STATE_OFFLINE      },
    { GW_STATE_RECONNECTING, GW_EVT_MAX_RETRIES,      GW_STATE_OFFLINE      },
    { GW_STATE_RECONNECTING, GW_EVT_USER_DISCONNECT,  GW_STATE_OFFLINE      },
    { GW_STATE_OFFLINE,      GW_EVT_DISCOVERED,       GW_STATE_DISCOVERING  },
};

#define NUM_TRANSITIONS (sizeof(TRANSITIONS) / sizeof(TRANSITIONS[0]))

bool gw_sm_handle_event(gw_state_t *state, gw_event_t event) {
    if (!state) return false;
    for (size_t i = 0; i < NUM_TRANSITIONS; i++) {
        if (TRANSITIONS[i].from == *state && TRANSITIONS[i].evt == event) {
            *state = TRANSITIONS[i].to;
            return true;
        }
    }
    return false;
}

const char *gw_sm_state_name(gw_state_t state) {
    switch (state) {
        case GW_STATE_UNKNOWN:      return "UNKNOWN";
        case GW_STATE_DISCOVERING:  return "DISCOVERING";
        case GW_STATE_CONNECTED:    return "CONNECTED";
        case GW_STATE_DEGRADED:     return "DEGRADED";
        case GW_STATE_RECONNECTING: return "RECONNECTING";
        case GW_STATE_OFFLINE:      return "OFFLINE";
        default:                    return "INVALID";
    }
}

const char *gw_sm_event_name(gw_event_t event) {
    switch (event) {
        case GW_EVT_DISCOVERED:        return "DISCOVERED";
        case GW_EVT_HANDSHAKE_OK:      return "HANDSHAKE_OK";
        case GW_EVT_HANDSHAKE_FAIL:    return "HANDSHAKE_FAIL";
        case GW_EVT_HEARTBEAT_OK:      return "HEARTBEAT_OK";
        case GW_EVT_HEARTBEAT_TIMEOUT: return "HEARTBEAT_TIMEOUT";
        case GW_EVT_MAX_RETRIES:       return "MAX_RETRIES";
        case GW_EVT_USER_DISCONNECT:   return "USER_DISCONNECT";
        default:                       return "INVALID";
    }
}
