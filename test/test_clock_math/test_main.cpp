#include <unity.h>

#include "ClockMath.h"

using namespace core;

void setUp() {}
void tearDown() {}

static void test_days_from_civil() {
    TEST_ASSERT_EQUAL_INT64(0, daysFromCivil(1970, 1, 1));
    TEST_ASSERT_EQUAL_INT64(18262, daysFromCivil(2020, 1, 1));
    TEST_ASSERT_EQUAL_INT64(-1, daysFromCivil(1969, 12, 31));
}

static void test_epoch_ms_from_utc() {
    // 2025-10-20T08:46:41Z, the ts in the protocol example (minus its milliseconds)
    auto ms = epochMsFromUtc(2025, 10, 20, 8, 46, 41);
    TEST_ASSERT_TRUE(ms.has_value());
    TEST_ASSERT_EQUAL_INT64(1760950001000LL, *ms);
}

static void test_epoch_ms_rejects_bad_fields() {
    TEST_ASSERT_FALSE(epochMsFromUtc(2026, 2, 29, 0, 0, 0).has_value());  // not a leap year
    TEST_ASSERT_TRUE(epochMsFromUtc(2028, 2, 29, 0, 0, 0).has_value());
    TEST_ASSERT_FALSE(epochMsFromUtc(2026, 13, 1, 0, 0, 0).has_value());
    TEST_ASSERT_FALSE(epochMsFromUtc(2026, 1, 1, 24, 0, 0).has_value());
}

static void test_protocol_floor_is_2020() {
    TEST_ASSERT_EQUAL_INT64(*epochMsFromUtc(2020, 1, 1, 0, 0, 0), kMinPlausibleEpochMs);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_days_from_civil);
    RUN_TEST(test_epoch_ms_from_utc);
    RUN_TEST(test_epoch_ms_rejects_bad_fields);
    RUN_TEST(test_protocol_floor_is_2020);
    return UNITY_END();
}
