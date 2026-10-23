#include <unity.h>

#include "MotionDetector.h"

using namespace core;

void setUp() {}
void tearDown() {}

using Change = MotionDetector::Change;

static const TrackerConfig kCfg;

// Metres north of a base point, as latitude.
static double north(double metres) { return -32.9364891 + metres / 111195.0; }
static constexpr double kLon = 151.7248993;

static MotionDetector::Input at(uint32_t t, double lat, FixQuality q = FixQuality::Fair, float err = 16,
                                std::optional<float> speed = std::nullopt) {
    MotionDetector::Input in;
    in.nowMs = t;
    in.latDeg = lat;
    in.lonDeg = kLon;
    in.quality = q;
    in.errorM = err;
    in.speedKmh = speed;
    return in;
}

static void test_window_jitter_stays_stationary() {
    // Real fixes from the window log: jumps of up to ~93 m and GPS speeds up to 26 km/h, all fair.
    MotionDetector d(kCfg);
    const double lats[] = {-32.9364891, -32.9363861, -32.9363632, -32.9364624, -32.9364433, -32.9365387,
                           -32.9364815, -32.9363441, -32.9366341, -32.9365463};
    const double lons[] = {151.7248993, 151.7239075, 151.7242126, 151.7247925, 151.7248993, 151.7246857,
                           151.7247467, 151.7248993, 151.7244568, 151.7245636};
    for (int i = 0; i < 10; ++i) {
        MotionDetector::Input in = at(i * 10000, lats[i], FixQuality::Fair, 18, 26.0f);
        in.lonDeg = lons[i];
        TEST_ASSERT_TRUE(d.update(in) == Change::None);
    }
    TEST_ASSERT_FALSE(d.moving());
}

static void test_leaving_the_anchor_is_motion() {
    MotionDetector d(kCfg);
    d.update(at(0, north(0)));
    TEST_ASSERT_TRUE(d.update(at(10000, north(200))) == Change::None);  // one fix outside: could be a jump
    TEST_ASSERT_TRUE(d.update(at(20000, north(400))) == Change::BecameMoving);
}

static void test_outlier_then_back_is_not_motion() {
    MotionDetector d(kCfg);
    d.update(at(0, north(0)));
    d.update(at(10000, north(300)));  // multipath outlier
    TEST_ASSERT_TRUE(d.update(at(20000, north(10))) == Change::None);
    TEST_ASSERT_TRUE(d.update(at(30000, north(300))) == Change::None);  // counter was reset
    TEST_ASSERT_FALSE(d.moving());
}

static void test_speed_counts_only_from_good_fixes() {
    MotionDetector d(kCfg);
    d.update(at(0, north(0)));
    d.update(at(10000, north(5), FixQuality::Fair, 30, 40.0f));
    TEST_ASSERT_TRUE(d.update(at(20000, north(10), FixQuality::Fair, 30, 40.0f)) == Change::None);
    d.update(at(30000, north(15), FixQuality::Good, 5, 40.0f));
    TEST_ASSERT_TRUE(d.update(at(40000, north(20), FixQuality::Good, 5, 40.0f)) == Change::BecameMoving);
}

static void test_poor_fixes_are_ignored() {
    MotionDetector d(kCfg);
    TEST_ASSERT_TRUE(d.update(at(0, north(5000), FixQuality::Poor, 800)) == Change::None);
    d.update(at(10000, north(0)));  // first usable fix is the anchor, not the poor one
    TEST_ASSERT_TRUE(d.update(at(20000, north(5000), FixQuality::Poor, 800)) == Change::None);
    TEST_ASSERT_TRUE(d.update(at(30000, north(5000), FixQuality::Poor, 800)) == Change::None);
    TEST_ASSERT_FALSE(d.moving());
}

static void test_large_error_needs_larger_move() {
    MotionDetector d(kCfg);
    d.update(at(0, north(0)));
    // 90 m error → radius 180 m: 170 m isn't enough.
    TEST_ASSERT_TRUE(d.update(at(10000, north(170), FixQuality::Fair, 90)) == Change::None);
    TEST_ASSERT_TRUE(d.update(at(20000, north(170), FixQuality::Fair, 90)) == Change::None);
    TEST_ASSERT_FALSE(d.moving());
}

static void test_ride_then_park() {
    MotionDetector d(kCfg);
    d.update(at(0, north(0)));
    d.update(at(10000, north(300)));
    TEST_ASSERT_TRUE(d.update(at(20000, north(600))) == Change::BecameMoving);
    // A minute at the lights, then on: still moving.
    TEST_ASSERT_TRUE(d.update(at(80000, north(600))) == Change::None);
    TEST_ASSERT_TRUE(d.update(at(90000, north(900))) == Change::None);
    // Parked with jitter for 5 minutes after the last progress at 90 s.
    for (uint32_t t = 100000; t < 90000 + 300000; t += 10000) {
        TEST_ASSERT_TRUE(d.update(at(t, north(900 + ((t / 10000) % 2 ? 60 : -60)))) == Change::None);
    }
    TEST_ASSERT_TRUE(d.update(at(390000, north(900))) == Change::BecameStationary);
    // New anchor is the parking spot: jitter there doesn't restart motion.
    TEST_ASSERT_TRUE(d.update(at(400000, north(960))) == Change::None);
    TEST_ASSERT_TRUE(d.update(at(410000, north(840))) == Change::None);
    TEST_ASSERT_FALSE(d.moving());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_window_jitter_stays_stationary);
    RUN_TEST(test_leaving_the_anchor_is_motion);
    RUN_TEST(test_outlier_then_back_is_not_motion);
    RUN_TEST(test_speed_counts_only_from_good_fixes);
    RUN_TEST(test_poor_fixes_are_ignored);
    RUN_TEST(test_large_error_needs_larger_move);
    RUN_TEST(test_ride_then_park);
    return UNITY_END();
}
