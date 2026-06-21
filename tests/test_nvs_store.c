#include "nvs_store.h"
#include "test.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define TEST_NVS_PATH "/tmp/gw_test_nvs.bin"

static void cleanup(void) {
    remove(TEST_NVS_PATH);
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s.compact_tmp", TEST_NVS_PATH);
    remove(tmp);
}

int main(void) {
    cleanup();

    gw_nvs_t *nvs = gw_nvs_open(TEST_NVS_PATH);
    GW_ASSERT(nvs != NULL);

    /* Set + get round-trip. */
    GW_ASSERT(gw_nvs_set(nvs, "ssid", "MyNetwork", 9));
    char buf[64] = {0};
    size_t out_len = 0;
    GW_ASSERT(gw_nvs_get(nvs, "ssid", buf, sizeof(buf), &out_len));
    GW_ASSERT_EQ(out_len, (size_t)9);
    GW_ASSERT(memcmp(buf, "MyNetwork", 9) == 0);

    /* Overwrite same key. */
    GW_ASSERT(gw_nvs_set(nvs, "ssid", "OtherNet", 8));
    GW_ASSERT(gw_nvs_get(nvs, "ssid", buf, sizeof(buf), &out_len));
    GW_ASSERT_EQ(out_len, (size_t)8);
    GW_ASSERT(memcmp(buf, "OtherNet", 8) == 0);
    GW_ASSERT_EQ(gw_nvs_entry_count(nvs), (size_t)1); /* still one logical key */

    /* Missing key. */
    GW_ASSERT(!gw_nvs_get(nvs, "nope", buf, sizeof(buf), &out_len));

    /* Truncation: out_len reports true length even if buf is smaller. */
    char small[3];
    GW_ASSERT(gw_nvs_get(nvs, "ssid", small, sizeof(small), &out_len));
    GW_ASSERT_EQ(out_len, (size_t)8);

    /* Erase. */
    uint32_t v = 42;
    GW_ASSERT(gw_nvs_set(nvs, "boot_count", &v, sizeof(v)));
    GW_ASSERT_EQ(gw_nvs_entry_count(nvs), (size_t)2);
    GW_ASSERT(gw_nvs_erase(nvs, "boot_count"));
    GW_ASSERT(!gw_nvs_get(nvs, "boot_count", buf, sizeof(buf), &out_len));
    GW_ASSERT_EQ(gw_nvs_entry_count(nvs), (size_t)1);
    GW_ASSERT(!gw_nvs_erase(nvs, "boot_count")); /* erasing again fails cleanly */

    gw_nvs_close(nvs);

    /* Reopen: replay from the log file must restore current state,
     * including the fact that boot_count was erased. */
    gw_nvs_t *nvs2 = gw_nvs_open(TEST_NVS_PATH);
    GW_ASSERT(nvs2 != NULL);
    GW_ASSERT(gw_nvs_get(nvs2, "ssid", buf, sizeof(buf), &out_len));
    GW_ASSERT_EQ(out_len, (size_t)8);
    GW_ASSERT(memcmp(buf, "OtherNet", 8) == 0);
    GW_ASSERT(!gw_nvs_get(nvs2, "boot_count", buf, sizeof(buf), &out_len));
    GW_ASSERT_EQ(gw_nvs_entry_count(nvs2), (size_t)1);

    /* Compaction must preserve live data and reduce on-disk size after
     * a history of overwrites/erases inflated the log. */
    long size_before = 0;
    {
        FILE *f = fopen(TEST_NVS_PATH, "rb");
        GW_ASSERT(f != NULL);
        fseek(f, 0, SEEK_END);
        size_before = ftell(f);
        fclose(f);
    }
    GW_ASSERT(gw_nvs_compact(nvs2));
    long size_after = 0;
    {
        FILE *f = fopen(TEST_NVS_PATH, "rb");
        GW_ASSERT(f != NULL);
        fseek(f, 0, SEEK_END);
        size_after = ftell(f);
        fclose(f);
    }
    GW_ASSERT(size_after < size_before);
    GW_ASSERT(gw_nvs_get(nvs2, "ssid", buf, sizeof(buf), &out_len));
    GW_ASSERT(memcmp(buf, "OtherNet", 8) == 0);

    gw_nvs_close(nvs2);
    cleanup();

    return GW_TEST_SUMMARY("nvs_store");
}
