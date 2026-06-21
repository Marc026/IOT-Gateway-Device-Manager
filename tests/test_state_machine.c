#include "state_machine.h"
#include "test.h"

int main(void) {
    gw_state_t s = GW_STATE_UNKNOWN;

    /* Illegal event from the initial state is rejected, state unchanged. */
    GW_ASSERT(!gw_sm_handle_event(&s, GW_EVT_HEARTBEAT_OK));
    GW_ASSERT_EQ(s, GW_STATE_UNKNOWN);

    /* Happy path: discover -> handshake -> connected. */
    GW_ASSERT(gw_sm_handle_event(&s, GW_EVT_DISCOVERED));
    GW_ASSERT_EQ(s, GW_STATE_DISCOVERING);
    GW_ASSERT(gw_sm_handle_event(&s, GW_EVT_HANDSHAKE_OK));
    GW_ASSERT_EQ(s, GW_STATE_CONNECTED);

    /* Heartbeats self-loop on CONNECTED. */
    GW_ASSERT(gw_sm_handle_event(&s, GW_EVT_HEARTBEAT_OK));
    GW_ASSERT_EQ(s, GW_STATE_CONNECTED);

    /* Escalation ladder: CONNECTED -> DEGRADED -> RECONNECTING -> OFFLINE. */
    GW_ASSERT(gw_sm_handle_event(&s, GW_EVT_HEARTBEAT_TIMEOUT));
    GW_ASSERT_EQ(s, GW_STATE_DEGRADED);
    GW_ASSERT(gw_sm_handle_event(&s, GW_EVT_HEARTBEAT_TIMEOUT));
    GW_ASSERT_EQ(s, GW_STATE_RECONNECTING);
    GW_ASSERT(gw_sm_handle_event(&s, GW_EVT_MAX_RETRIES));
    GW_ASSERT_EQ(s, GW_STATE_OFFLINE);

    /* A device can rejoin after going offline. */
    GW_ASSERT(gw_sm_handle_event(&s, GW_EVT_DISCOVERED));
    GW_ASSERT_EQ(s, GW_STATE_DISCOVERING);

    /* DEGRADED can recover directly back to CONNECTED on a fresh heartbeat. */
    gw_state_t s2 = GW_STATE_CONNECTED;
    GW_ASSERT(gw_sm_handle_event(&s2, GW_EVT_HEARTBEAT_TIMEOUT));
    GW_ASSERT_EQ(s2, GW_STATE_DEGRADED);
    GW_ASSERT(gw_sm_handle_event(&s2, GW_EVT_HEARTBEAT_OK));
    GW_ASSERT_EQ(s2, GW_STATE_CONNECTED);

    /* USER_DISCONNECT is accepted from every "live" state and lands on OFFLINE. */
    gw_state_t live_states[] = { GW_STATE_CONNECTED, GW_STATE_DEGRADED, GW_STATE_RECONNECTING };
    for (size_t i = 0; i < sizeof(live_states) / sizeof(live_states[0]); i++) {
        gw_state_t s3 = live_states[i];
        GW_ASSERT(gw_sm_handle_event(&s3, GW_EVT_USER_DISCONNECT));
        GW_ASSERT_EQ(s3, GW_STATE_OFFLINE);
    }

    /* Sanity check name lookups don't crash / return something for every enum value. */
    for (int st = 0; st < GW_STATE_COUNT; st++) {
        GW_ASSERT(gw_sm_state_name((gw_state_t)st) != NULL);
    }

    return GW_TEST_SUMMARY("state_machine");
}
