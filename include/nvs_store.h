#ifndef GW_NVS_STORE_H
#define GW_NVS_STORE_H

#include <stddef.h>
#include <stdbool.h>

/* Emulates a flash-backed non-volatile key/value store (the same role
 * ESP-IDF's NVS or a TI/Nordic settings partition plays on real hardware):
 * - log-structured: every set/erase is an *appended* record, never an
 *   in-place rewrite, because flash cells must be block-erased before
 *   they can be rewritten.
 * - gw_nvs_compact() reclaims space by rewriting only the live records,
 *   standing in for the garbage-collection pass a flash translation layer
 *   runs to reclaim stale pages.
 * On this host build the "flash" is just a regular file; the API is
 * shaped so that swapping the backing store for a real flash driver only
 * touches this module. */

#define NVS_MAX_KEY_LEN   15
#define NVS_MAX_VAL_LEN   4096
#define NVS_MAX_ENTRIES   32

typedef struct gw_nvs gw_nvs_t;

/* Opens (creating if needed) the log file at `path` and replays it into
 * an in-memory cache. Returns NULL on I/O failure. */
gw_nvs_t *gw_nvs_open(const char *path);
void gw_nvs_close(gw_nvs_t *nvs);

bool gw_nvs_set(gw_nvs_t *nvs, const char *key, const void *value, size_t len);

/* Copies up to buf_cap bytes into buf. *out_len is always set to the
 * stored value's true length (even if buf_cap was too small) so the
 * caller can detect truncation. Returns false if the key is absent. */
bool gw_nvs_get(gw_nvs_t *nvs, const char *key, void *buf, size_t buf_cap, size_t *out_len);

bool gw_nvs_erase(gw_nvs_t *nvs, const char *key);

/* Rewrites the backing file keeping only the latest value for each live
 * key. Safe to call at any time; cheap to call rarely. */
bool gw_nvs_compact(gw_nvs_t *nvs);

size_t gw_nvs_entry_count(const gw_nvs_t *nvs);

#endif /* GW_NVS_STORE_H */
