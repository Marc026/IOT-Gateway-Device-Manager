#include "nvs_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define NVS_TOMBSTONE  ((uint32_t)0xFFFFFFFFu)

typedef struct {
    bool   used;
    char   key[NVS_MAX_KEY_LEN + 1];
    uint8_t val[NVS_MAX_VAL_LEN];
    size_t val_len;
} nvs_cache_entry_t;

struct gw_nvs {
    char path[512];
    FILE *fp;
    nvs_cache_entry_t entries[NVS_MAX_ENTRIES];
};

static int find_slot(struct gw_nvs *nvs, const char *key) {
    for (int i = 0; i < NVS_MAX_ENTRIES; i++) {
        if (nvs->entries[i].used && strcmp(nvs->entries[i].key, key) == 0) return i;
    }
    return -1;
}

static int find_free_slot(struct gw_nvs *nvs) {
    for (int i = 0; i < NVS_MAX_ENTRIES; i++) {
        if (!nvs->entries[i].used) return i;
    }
    return -1;
}

static bool write_record(FILE *fp, const char *key, const void *val, uint32_t val_len) {
    uint8_t key_len = (uint8_t)strlen(key);
    if (fwrite(&key_len, 1, 1, fp) != 1) return false;
    if (fwrite(key, 1, key_len, fp) != key_len) return false;
    if (fwrite(&val_len, sizeof(val_len), 1, fp) != 1) return false;
    if (val_len != NVS_TOMBSTONE && val_len > 0) {
        if (fwrite(val, 1, val_len, fp) != val_len) return false;
    }
    return fflush(fp) == 0;
}

static void replay(struct gw_nvs *nvs) {
    fseek(nvs->fp, 0, SEEK_SET);
    for (;;) {
        uint8_t key_len;
        if (fread(&key_len, 1, 1, nvs->fp) != 1) break; /* clean EOF */
        if (key_len > NVS_MAX_KEY_LEN) break;            /* corrupt tail, stop */

        char key[NVS_MAX_KEY_LEN + 1];
        if (fread(key, 1, key_len, nvs->fp) != key_len) break;
        key[key_len] = '\0';

        uint32_t val_len;
        if (fread(&val_len, sizeof(val_len), 1, nvs->fp) != 1) break;

        if (val_len == NVS_TOMBSTONE) {
            int idx = find_slot(nvs, key);
            if (idx >= 0) nvs->entries[idx].used = false;
            continue;
        }
        if (val_len > NVS_MAX_VAL_LEN) break;

        uint8_t valbuf[NVS_MAX_VAL_LEN];
        if (val_len > 0 && fread(valbuf, 1, val_len, nvs->fp) != val_len) break;

        int idx = find_slot(nvs, key);
        if (idx < 0) idx = find_free_slot(nvs);
        if (idx < 0) continue; /* table full; drop, shouldn't happen in practice */

        memcpy(nvs->entries[idx].key, key, key_len);
        nvs->entries[idx].key[key_len] = '\0';
        if (val_len > 0) memcpy(nvs->entries[idx].val, valbuf, val_len);
        nvs->entries[idx].val_len = val_len;
        nvs->entries[idx].used = true;
    }
    fseek(nvs->fp, 0, SEEK_END);
}

gw_nvs_t *gw_nvs_open(const char *path) {
    struct gw_nvs *nvs = calloc(1, sizeof(*nvs));
    if (!nvs) return NULL;
    size_t plen = strlen(path);
    if (plen > sizeof(nvs->path) - 1) plen = sizeof(nvs->path) - 1;
    memcpy(nvs->path, path, plen);
    nvs->path[plen] = '\0';

    nvs->fp = fopen(path, "a+b");
    if (!nvs->fp) { free(nvs); return NULL; }

    replay(nvs);
    return nvs;
}

void gw_nvs_close(gw_nvs_t *nvs) {
    if (!nvs) return;
    if (nvs->fp) fclose(nvs->fp);
    free(nvs);
}

bool gw_nvs_set(gw_nvs_t *nvs, const char *key, const void *value, size_t len) {
    if (!nvs || !key || strlen(key) > NVS_MAX_KEY_LEN || len > NVS_MAX_VAL_LEN) return false;

    int idx = find_slot(nvs, key);
    if (idx < 0) idx = find_free_slot(nvs);
    if (idx < 0) return false; /* table full */

    if (!write_record(nvs->fp, key, value, (uint32_t)len)) return false;

    size_t key_len = strlen(key);
    memcpy(nvs->entries[idx].key, key, key_len);
    nvs->entries[idx].key[key_len] = '\0';
    if (len > 0) memcpy(nvs->entries[idx].val, value, len);
    nvs->entries[idx].val_len = len;
    nvs->entries[idx].used = true;
    return true;
}

bool gw_nvs_get(gw_nvs_t *nvs, const char *key, void *buf, size_t buf_cap, size_t *out_len) {
    if (!nvs || !key) return false;
    int idx = find_slot(nvs, key);
    if (idx < 0) return false;

    if (out_len) *out_len = nvs->entries[idx].val_len;
    size_t to_copy = nvs->entries[idx].val_len < buf_cap ? nvs->entries[idx].val_len : buf_cap;
    if (buf && to_copy > 0) memcpy(buf, nvs->entries[idx].val, to_copy);
    return true;
}

bool gw_nvs_erase(gw_nvs_t *nvs, const char *key) {
    if (!nvs || !key) return false;
    int idx = find_slot(nvs, key);
    if (idx < 0) return false;
    if (!write_record(nvs->fp, key, NULL, NVS_TOMBSTONE)) return false;
    nvs->entries[idx].used = false;
    return true;
}

bool gw_nvs_compact(gw_nvs_t *nvs) {
    if (!nvs) return false;

    char tmp_path[600];
    snprintf(tmp_path, sizeof(tmp_path), "%s.compact_tmp", nvs->path);

    FILE *tmp = fopen(tmp_path, "wb");
    if (!tmp) return false;

    for (int i = 0; i < NVS_MAX_ENTRIES; i++) {
        if (!nvs->entries[i].used) continue;
        if (!write_record(tmp, nvs->entries[i].key, nvs->entries[i].val,
                           (uint32_t)nvs->entries[i].val_len)) {
            fclose(tmp);
            remove(tmp_path);
            return false;
        }
    }
    fclose(tmp);

    fclose(nvs->fp);
    if (rename(tmp_path, nvs->path) != 0) {
        nvs->fp = fopen(nvs->path, "a+b"); /* try to restore a usable handle */
        return false;
    }
    nvs->fp = fopen(nvs->path, "a+b");
    return nvs->fp != NULL;
}

size_t gw_nvs_entry_count(const gw_nvs_t *nvs) {
    if (!nvs) return 0;
    size_t n = 0;
    for (int i = 0; i < NVS_MAX_ENTRIES; i++) if (nvs->entries[i].used) n++;
    return n;
}
