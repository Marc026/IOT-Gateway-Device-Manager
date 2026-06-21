#ifndef GW_WATCHDOG_H
#define GW_WATCHDOG_H

#include <stdint.h>
#include <stdbool.h>

/* Software task watchdog. Unlike a hardware WDT that resets the whole
 * MCU, this models the more targeted pattern used in connectivity
 * firmware: individual subsystems (the network RX task, the storage
 * task, ...) check in ("kick") on a schedule, and a subsystem that goes
 * quiet gets its *own* recovery callback invoked rather than rebooting
 * the entire device over one wedged task. */

#define WDT_MAX_TASKS        8
#define WDT_HANDLE_INVALID  (-1)

typedef void (*wdt_recovery_fn)(void *ctx);

typedef struct gw_wdt gw_wdt_t;

gw_wdt_t *gw_wdt_create(void);
void gw_wdt_destroy(gw_wdt_t *wdt);

/* Registers a monitored task. `timeout_ms` is the max allowed gap since
 * the last kick before a single miss is counted at the next check; after
 * `max_misses` *consecutive* gw_wdt_check() calls that observe an overdue
 * kick, `recovery` fires once and the miss counter resets. Returns a
 * handle >= 0, or WDT_HANDLE_INVALID if the table is full. */
int gw_wdt_register(gw_wdt_t *wdt, const char *name, uint32_t timeout_ms,
                     uint8_t max_misses, wdt_recovery_fn recovery, void *ctx,
                     uint64_t now_ms);

void gw_wdt_kick(gw_wdt_t *wdt, int handle, uint64_t now_ms);

/* Call periodically from the main loop. Returns the number of recovery
 * callbacks fired during this call. */
int gw_wdt_check(gw_wdt_t *wdt, uint64_t now_ms);

#endif /* GW_WATCHDOG_H */
