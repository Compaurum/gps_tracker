#include <unity.h>

#include "FixQuality.h"
#include "Geo.h"

using namespace core;

void setUp() {}
void tearDown() {}

static const TrackerConfig kCfg;

static GnssReading locked() {
    GnssReading r;
    r.hasFix = true;
    r.sats = 10;
    return r;
}

static void test_receiver_estimate_sets_class() {
    GnssReading r = locked();
    r.accuracyM = 8;
    TEST_ASSERT_TRUE(assessAccuracy(r, kCfg).quality == FixQuality::Good);
    TEST_ASSERT_TRUE(assessAccuracy(r, kCfg).fromReceiver);
    r.accuracyM = 60;
    TEST_ASSERT_TRUE(assessAccuracy(r, kCfg).quality == FixQuality::Fair);
    r.accuracyM = 300;
    TEST_ASSERT_TRUE(assessAccuracy(r, kCfg).quality == FixQuality::Poor);
}

static void test_hdop_alone_is_never_good() {
    // The window case: HDOP 1.6 looks fine, the position jumped ~90 m.
    GnssReading r = locked();
    r.hdop = 1.6f;
    auto a = assessAccuracy(r, kCfg);
    TEST_ASSERT_TRUE(a.quality == FixQuality::Fair);
    TEST_ASSERT_FALSE(a.fromReceiver);
    TEST_ASSERT_FLOAT_WITHIN(1e-3, 16.0f, a.errorM);
    r.hdop = 12.0f;
    TEST_ASSERT_TRUE(assessAccuracy(r, kCfg).quality == FixQuality::Poor);
}

static void test_no_lock_few_sats_or_no_estimate_are_poor() {
    GnssReading r = locked();
    TEST_ASSERT_TRUE(assessAccuracy(r, kCfg).quality == FixQuality::Poor);  // no estimate at all
    r.accuracyM = 5;
    r.sats = 3;
    TEST_ASSERT_TRUE(assessAccuracy(r, kCfg).quality == FixQuality::Poor);
    r.sats = 10;
    r.hasFix = false;
    TEST_ASSERT_TRUE(assessAccuracy(r, kCfg).quality == FixQuality::Poor);
}

static void test_distance() {
    // Two readings from the window log, ~93 m apart.
    TEST_ASSERT_DOUBLE_WITHIN(2.0, 93.0, distanceM(-32.9364891, 151.7248993, -32.9363861, 151.7239075));
    TEST_ASSERT_DOUBLE_WITHIN(1.0, 111195.0, distanceM(0, 0, 1, 0));  // one degree of latitude
    TEST_ASSERT_DOUBLE_WITHIN(1.0, 111195.0, distanceM(0, 179.5, 0, -179.5));  // across the antimeridian
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_receiver_estimate_sets_class);
    RUN_TEST(test_hdop_alone_is_never_good);
    RUN_TEST(test_no_lock_few_sats_or_no_estimate_are_poor);
    RUN_TEST(test_distance);
    return UNITY_END();
}
