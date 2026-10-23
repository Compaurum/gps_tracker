#include <unity.h>

#include <cmath>

#include "ClockMath.h"
#include "GnssParse.h"

using namespace core;

void setUp() {}
void tearDown() {}

// Layout as captured by LilyGO on A76xx hardware (TinyGSM fork, getGPSImpl), with real numbers.
static const char* kFix3d =
    "+CGNSSINFO: 3,13,14,,,32.928312,S,151.781705,E,201025,084641.00,12.3,2.268,270.0,1.7,0.9,1.1,";

static void test_3d_fix() {
    auto r = parseCgnssInfo(kFix3d);
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_TRUE(r->hasFix);
    TEST_ASSERT_EQUAL_INT64(1760950001000LL, r->timeMs);  // 2025-10-20T08:46:41Z
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, -32.928312, r->latDeg);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 151.781705, r->lonDeg);
    TEST_ASSERT_FLOAT_WITHIN(1e-3, 12.3f, *r->altM);
    TEST_ASSERT_FLOAT_WITHIN(1e-2, 4.2f, *r->speedKmh);  // 2.268 knots
    TEST_ASSERT_FLOAT_WITHIN(1e-3, 270.0f, *r->courseDeg);
    TEST_ASSERT_FLOAT_WITHIN(1e-3, 0.9f, *r->hdop);
    TEST_ASSERT_EQUAL(27, *r->sats);  // 13 GPS + 14 BeiDou
}

static void test_no_lock_has_empty_fields() {
    auto r = parseCgnssInfo("+CGNSSINFO: ,,,,,,,,,,,,,,,,");
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_FALSE(r->hasFix);
    auto bare = parseCgnssInfo("+CGNSSINFO:");
    TEST_ASSERT_TRUE(bare.has_value());
    TEST_ASSERT_FALSE(bare->hasFix);
}

static void test_west_north_and_fractional_seconds() {
    auto r = parseCgnssInfo("+CGNSSINFO: 2,05,,,,51.5,N,0.12,W,010126,000000.25,,0.0,,,,");
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_TRUE(r->hasFix);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 51.5, r->latDeg);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, -0.12, r->lonDeg);
    TEST_ASSERT_EQUAL_INT64(*epochMsFromUtc(2026, 1, 1, 0, 0, 0) + 250, r->timeMs);
    TEST_ASSERT_FALSE(r->altM.has_value());
    TEST_ASSERT_FALSE(r->courseDeg.has_value());
    TEST_ASSERT_FALSE(r->hdop.has_value());
    TEST_ASSERT_EQUAL(5, *r->sats);
}

static void test_malformed_lock_is_rejected() {
    TEST_ASSERT_FALSE(parseCgnssInfo("+CGNSSINFO: 3,13,14,,,abc,S,151.78,E,201025,084641.00,12.3,0,0,1,1,1").has_value());
    TEST_ASSERT_FALSE(parseCgnssInfo("+CGNSSINFO: 3,13,14,,,32.9,X,151.78,E,201025,084641.00,12.3,0,0,1,1,1").has_value());
    TEST_ASSERT_FALSE(parseCgnssInfo("+CGNSSINFO: 3,13,14,,,32.9,S,151.78,E,991325,084641.00,12.3,0,0,1,1,1").has_value());
    TEST_ASSERT_FALSE(parseCgnssInfo("+CGNSSINFO: 3,13,14").has_value());
    TEST_ASSERT_FALSE(parseCgnssInfo("+CSQ: 20,99").has_value());
}

static void test_truncated_line_from_the_a7670() {
    // Real 98-character answer, cut inside VDOP by the modem.
    auto r = parseCgnssInfo(
        "+CGNSSINFO: 3,09,,01,00,32.9365387,S,151.7246857,E,211026,233351.00,66.4,14.143,80.14,3.65,2.11,2.");
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_TRUE(r->hasFix);
    TEST_ASSERT_FLOAT_WITHIN(1e-3, 2.11f, *r->hdop);
    // Cut much earlier, inside the speed: position and time survive, speed is dropped.
    auto early = parseCgnssInfo("+CGNSSINFO: 3,09,,01,00,32.9365387,S,151.7246857,E,211026,233351.00,66.4,14.1");
    TEST_ASSERT_TRUE(early.has_value());
    TEST_ASSERT_TRUE(early->hasFix);
    TEST_ASSERT_FALSE(early->hdop.has_value());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_3d_fix);
    RUN_TEST(test_no_lock_has_empty_fields);
    RUN_TEST(test_west_north_and_fractional_seconds);
    RUN_TEST(test_malformed_lock_is_rejected);
    RUN_TEST(test_truncated_line_from_the_a7670);
    return UNITY_END();
}
