#include "watchdog.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    bool used;
    char name[16];
    uint32_t timeout_ms;
    uint8_t  max_misses;
    uint8_t  misses;
    uint64_t last_kick_ms;
    wdt_recovery_fn recovery;
    void *ctx;
} wdt_task_t;

struct gw_wdt {
    wdt_task_t tasks[WDT_MAX_TASKS];
};

gw_wdt_t *gw_wdt_create(void) {
    return calloc(1, sizeof(struct gw_wdt));
}

void gw_wdt_destroy(gw_wdt_t *wdt) {
    free(wdt);
}

int gw_wdt_register(gw_wdt_t *wdt, const char *name, uint32_t timeout_ms,
                     uint8_t max_misses, wdt_recovery_fn recovery, void *ctx,
                     uint64_t now_ms) {
    if (!wdt) return WDT_HANDLE_INVALID;
    for (int i = 0; i < WDT_MAX_TASKS; i++) {
        if (!wdt->tasks[i].used) {
            wdt_task_t *t = &wdt->tasks[i];
            t->used = true;
            const char *src = name ? name : "";
            size_t nlen = strlen(src);
            if (nlen > sizeof(t->name) - 1) nlen = sizeof(t->name) - 1;
            memcpy(t->name, src, nlen);
            t->name[nlen] = '\0';
            t->timeout_ms = timeout_ms;
            t->max_misses = max_misses;
            t->misses = 0;
            t->last_kick_ms = now_ms;
            t->recovery = recovery;
            t->ctx = ctx;
            return i;
        }
    }
    return WDT_HANDLE_INVALID;
}

void gw_wdt_kick(gw_wdt_t *wdt, int handle, uint64_t now_ms) {
    if (!wdt || handle < 0 || handle >= WDT_MAX_TASKS) return;
    wdt_task_t *t = &wdt->tasks[handle];
    if (!t->used) return;
    t->last_kick_ms = now_ms;
    t->misses = 0;
}

int gw_wdt_check(gw_wdt_t *wdt, uint64_t now_ms) {
    if (!wdt) return 0;
    int fired = 0;
    for (int i = 0; i < WDT_MAX_TASKS; i++) {
        wdt_task_t *t = &wdt->tasks[i];
        if (!t->used) continue;
        if (now_ms - t->last_kick_ms <= t->timeout_ms) continue;

        t->misses++;
        if (t->misses >= t->max_misses) {
            if (t->recovery) t->recovery(t->ctx);
            t->misses = 0;
            t->last_kick_ms = now_ms; /* recovery implicitly re-arms the deadline */
            fired++;
        }
    }
    return fired;
}
