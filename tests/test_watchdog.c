#include "watchdog.h"
#include "test.h"

static int g_fire_count = 0;
static void recovery_cb(void *ctx) {
    int *counter = (int *)ctx;
    (*counter)++;
}

int main(void) {
    gw_wdt_t *wdt = gw_wdt_create();
    GW_ASSERT(wdt != NULL);

    int handle = gw_wdt_register(wdt, "net_rx", 100, 2, recovery_cb, &g_fire_count, 0);
    GW_ASSERT(handle != WDT_HANDLE_INVALID);

    /* Within the deadline: no miss, no fire. */
    GW_ASSERT_EQ(gw_wdt_check(wdt, 50), 0);

    /* Overdue once: counted as a miss but max_misses(2) not yet reached. */
    GW_ASSERT_EQ(gw_wdt_check(wdt, 150), 0);
    GW_ASSERT_EQ(g_fire_count, 0);

    /* Overdue again without a kick in between: second consecutive miss
     * reaches max_misses -> recovery fires exactly once. */
    GW_ASSERT_EQ(gw_wdt_check(wdt, 260), 1);
    GW_ASSERT_EQ(g_fire_count, 1);

    /* Recovery re-arms the deadline; immediately re-checking should not
     * fire again. */
    GW_ASSERT_EQ(gw_wdt_check(wdt, 270), 0);
    GW_ASSERT_EQ(g_fire_count, 1);

    /* A kick resets the miss counter and the deadline. */
    gw_wdt_kick(wdt, handle, 300);
    GW_ASSERT_EQ(gw_wdt_check(wdt, 350), 0); /* well within 100ms of the kick */

    /* Table-full behavior. */
    gw_wdt_t *full = gw_wdt_create();
    int last_handle = WDT_HANDLE_INVALID;
    for (int i = 0; i < WDT_MAX_TASKS; i++) {
        last_handle = gw_wdt_register(full, "t", 1000, 1, NULL, NULL, 0);
        GW_ASSERT(last_handle != WDT_HANDLE_INVALID);
    }
    GW_ASSERT_EQ(gw_wdt_register(full, "overflow", 1000, 1, NULL, NULL, 0), WDT_HANDLE_INVALID);

    gw_wdt_destroy(full);
    gw_wdt_destroy(wdt);
    return GW_TEST_SUMMARY("watchdog");
}
