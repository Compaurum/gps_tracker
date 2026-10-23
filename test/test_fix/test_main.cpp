#include <unity.h>

#include "Fix.h"

using namespace core;

void setUp() {}
void tearDown() {}

static GnssReading good() {
    GnssReading r;
    r.hasFix = true;
    r.timeMs = 1760950001400LL;
    r.latDeg = -32.928312;
    r.lonDeg = 151.781705;
    r.altM = 12.6f;
    r.speedKmh = 4.2f;
    r.courseDeg = 270.0f;
    r.hdop = 0.9f;
    r.sats = 9;
    return r;
}

static void test_packs_all_fields() {
    auto f = fixFromReading(good());
    TEST_ASSERT_TRUE(f.has_value());
    TEST_ASSERT_EQUAL_UINT32(1760950001u, f->tsS);  // whole seconds
    TEST_ASSERT_EQUAL_INT32(-329283120, f->latE7);
    TEST_ASSERT_EQUAL_INT32(1517817050, f->lonE7);
    TEST_ASSERT_EQUAL_INT16(13, f->altM);
    TEST_ASSERT_EQUAL_UINT16(420, f->speedCkmh);
    TEST_ASSERT_EQUAL_UINT16(27000, f->courseCdeg);
    TEST_ASSERT_EQUAL_UINT8(9, f->sats);
    TEST_ASSERT_EQUAL_UINT8(9, f->hdopDeci);
    TEST_ASSERT_EQUAL_UINT8(Fix::kAlt | Fix::kSpeed | Fix::kCourse | Fix::kSats | Fix::kHdop, f->flags);
}

static void test_optional_fields_absent() {
    GnssReading r = good();
    r.altM.reset();
    r.speedKmh.reset();
    r.courseDeg.reset();
    r.hdop.reset();
    r.sats.reset();
    auto f = fixFromReading(r);
    TEST_ASSERT_TRUE(f.has_value());
    TEST_ASSERT_EQUAL_UINT8(0, f->flags);
}

static void test_protocol_rejections() {
    GnssReading r = good();
    r.hasFix = false;
    TEST_ASSERT_FALSE(fixFromReading(r).has_value());  // no lock

    r = good();
    r.latDeg = 0;
    r.lonDeg = 0;
    TEST_ASSERT_FALSE(fixFromReading(r).has_value());  // exactly 0,0

    r = good();
    r.latDeg = 3255.69;  // ddmm.mm format would land here
    TEST_ASSERT_FALSE(fixFromReading(r).has_value());

    r = good();
    r.lonDeg = -180.5;
    TEST_ASSERT_FALSE(fixFromReading(r).has_value());

    r = good();
    r.timeMs = 1000;  // receiver time not set
    TEST_ASSERT_FALSE(fixFromReading(r).has_value());
}

static void test_accuracy_and_clear_motion() {
    GnssReading r = good();
    r.accuracyM = 7.6f;
    auto f = fixFromReading(r);
    TEST_ASSERT_TRUE(f->has(Fix::kAcc));
    TEST_ASSERT_EQUAL_UINT16(8, f->accM);
    f->clearMotion();
    TEST_ASSERT_FALSE(f->has(Fix::kSpeed));
    TEST_ASSERT_FALSE(f->has(Fix::kCourse));
    TEST_ASSERT_TRUE(f->has(Fix::kAlt));
}

static void test_values_saturate() {
    GnssReading r = good();
    r.hdop = 99.0f;
    r.speedKmh = 1000.0f;
    auto f = fixFromReading(r);
    TEST_ASSERT_EQUAL_UINT8(255, f->hdopDeci);
    TEST_ASSERT_EQUAL_UINT16(65535, f->speedCkmh);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_packs_all_fields);
    RUN_TEST(test_optional_fields_absent);
    RUN_TEST(test_protocol_rejections);
    RUN_TEST(test_accuracy_and_clear_motion);
    RUN_TEST(test_values_saturate);
    return UNITY_END();
}
