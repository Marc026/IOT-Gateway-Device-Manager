#ifndef GW_TEST_H
#define GW_TEST_H

/* Minimal, dependency-free test harness. Each test_*.c file is its own
 * translation unit / executable, so the file-scope counters below do not
 * violate ODR across the test suite. Deliberately avoids pulling in a
 * third-party framework -- consistent with the no-heap-surprises, few
 * external deps mindset that applies to constrained targets. */
#include <stdio.h>

static int gw_test_pass = 0;
static int gw_test_fail = 0;

#define GW_ASSERT(cond) do { \
    if (cond) { gw_test_pass++; } \
    else { \
        gw_test_fail++; \
        fprintf(stderr, "  FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
    } \
} while (0)

#define GW_ASSERT_EQ(a, b) GW_ASSERT((a) == (b))
#define GW_ASSERT_STREQ(a, b) GW_ASSERT(strcmp((a), (b)) == 0)

#define GW_TEST_SUMMARY(suite_name) \
    (printf("[%s] %d passed, %d failed\n", (suite_name), gw_test_pass, gw_test_fail), \
     gw_test_fail == 0 ? 0 : 1)

#endif /* GW_TEST_H */
