#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "Nmea.h"

using namespace core;

void setUp() {}
void tearDown() {}

// Wraps a sentence body in "$...*hh" with a correct checksum.
static std::string nmea(const char* body) {
    unsigned sum = 0;
    for (const char* p = body; *p; ++p) sum ^= static_cast<unsigned char>(*p);
    char tail[8];
    snprintf(tail, sizeof(tail), "*%02X", sum);
    return std::string("$") + body + tail;
}

// One epoch at 2025-10-20T08:46:41Z, Newcastle-ish, from a real-world style receiver.
static const char* kRmc = "GNRMC,084641.00,A,3255.69872,S,15146.90230,E,2.268,270.0,201025,,,A";
static const char* kGga = "GNGGA,084641.00,3255.69872,S,15146.90230,E,1,10,0.9,12.3,M,24.1,M,,";
static const char* kGst = "GNGST,084641.00,4.1,6.0,3.0,12.0,3.0,4.0,7.5";

static void test_checksum() {
    TEST_ASSERT_TRUE(nmeaChecksumOk("$GPGLL,4916.45,N,12311.12,W,225444,A,*1D"));  // NMEA reference example
    TEST_ASSERT_TRUE(nmeaChecksumOk(nmea(kRmc).c_str()));
    std::string bad = nmea(kRmc);
    bad[10] = '9';
    TEST_ASSERT_FALSE(nmeaChecksumOk(bad.c_str()));
    TEST_ASSERT_FALSE(nmeaChecksumOk("$GNRMC,084641.00,A"));  // no checksum
    TEST_ASSERT_FALSE(nmeaChecksumOk("+CGNSSINFO: 3,09"));
}

static void test_full_epoch() {
    NmeaAssembler a;
    TEST_ASSERT_TRUE(a.feed(nmea(kGga).c_str(), 1000));
    TEST_ASSERT_TRUE(a.feed(nmea(kRmc).c_str(), 1000));
    TEST_ASSERT_TRUE(a.feed(nmea(kGst).c_str(), 1000));
    auto r = a.latest(1500, 3000);
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_TRUE(r->hasFix);
    TEST_ASSERT_EQUAL_INT64(1760950001000LL, r->timeMs);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, -32.928312, r->latDeg);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 151.781705, r->lonDeg);
    TEST_ASSERT_FLOAT_WITHIN(1e-2, 4.2f, *r->speedKmh);
    TEST_ASSERT_FLOAT_WITHIN(1e-3, 270.0f, *r->courseDeg);
    TEST_ASSERT_EQUAL(10, *r->sats);
    TEST_ASSERT_FLOAT_WITHIN(1e-3, 0.9f, *r->hdop);
    TEST_ASSERT_FLOAT_WITHIN(1e-3, 12.3f, *r->altM);
    TEST_ASSERT_FLOAT_WITHIN(1e-3, 5.0f, *r->accuracyM);  // sqrt(3^2 + 4^2)
    TEST_ASSERT_TRUE(a.gstSeen());
}

static void test_other_epochs_do_not_mix() {
    NmeaAssembler a;
    a.feed(nmea(kRmc).c_str(), 1000);
    a.feed(nmea("GNGGA,084651.00,3255.69872,S,15146.90230,E,1,10,0.9,12.3,M,24.1,M,,").c_str(), 1000);
    a.feed(nmea("GNGST,084631.00,4.1,6.0,3.0,12.0,3.0,4.0,7.5").c_str(), 1000);
    auto r = a.latest(1000, 3000);
    TEST_ASSERT_TRUE(r->hasFix);
    TEST_ASSERT_FALSE(r->sats.has_value());
    TEST_ASSERT_FALSE(r->accuracyM.has_value());
}

static void test_void_and_dead_reckoning_are_no_fix() {
    NmeaAssembler a;
    a.feed(nmea("GNRMC,084641.00,V,,,,,,,201025,,,N").c_str(), 1000);
    auto r = a.latest(1000, 3000);
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_FALSE(r->hasFix);

    a.feed(nmea("GNRMC,084641.00,A,3255.69872,S,15146.90230,E,2.268,270.0,201025,,,E").c_str(), 2000);
    TEST_ASSERT_FALSE(a.latest(2000, 3000)->hasFix);

    a.feed(nmea(kRmc).c_str(), 3000);
    a.feed(nmea("GNGGA,084641.00,3255.69872,S,15146.90230,E,6,10,0.9,12.3,M,24.1,M,,").c_str(), 3000);
    TEST_ASSERT_FALSE(a.latest(3000, 3000)->hasFix);
}

static void test_silent_receiver_and_bad_checksum() {
    NmeaAssembler a;
    TEST_ASSERT_FALSE(a.latest(0, 3000).has_value());
    a.feed(nmea(kRmc).c_str(), 1000);
    TEST_ASSERT_TRUE(a.latest(4000, 3000).has_value());
    TEST_ASSERT_FALSE(a.latest(4001, 3000).has_value());

    std::string bad = nmea(kRmc);
    bad[bad.size() - 1] = bad[bad.size() - 1] == '0' ? '1' : '0';
    TEST_ASSERT_TRUE(a.feed(bad.c_str(), 5000));
    TEST_ASSERT_EQUAL_UINT32(1, a.badChecksums());
    TEST_ASSERT_FALSE(a.latest(5000, 3000).has_value());  // the bad one wasn't taken
}

static void test_non_nmea_lines_are_left_alone() {
    NmeaAssembler a;
    TEST_ASSERT_FALSE(a.feed("+CMQTTPUB: 0,0", 0));
    TEST_ASSERT_FALSE(a.feed("OK", 0));
    TEST_ASSERT_FALSE(a.anySentence());
}

static void test_gst_without_estimate() {
    NmeaAssembler a;
    a.feed(nmea(kRmc).c_str(), 1000);
    a.feed(nmea("GNGST,084641.00,,,,,,,").c_str(), 1000);
    TEST_ASSERT_FALSE(a.latest(1000, 3000)->accuracyM.has_value());
    TEST_ASSERT_FALSE(a.gstSeen());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_checksum);
    RUN_TEST(test_full_epoch);
    RUN_TEST(test_other_epochs_do_not_mix);
    RUN_TEST(test_void_and_dead_reckoning_are_no_fix);
    RUN_TEST(test_silent_receiver_and_bad_checksum);
    RUN_TEST(test_non_nmea_lines_are_left_alone);
    RUN_TEST(test_gst_without_estimate);
    return UNITY_END();
}
