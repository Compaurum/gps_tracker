#include <unity.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "ProtocolEncoder.h"

using namespace core;

static constexpr int64_t kTs = 1760950001000LL;

void setUp() {}
void tearDown() {}

static void test_boot_matches_protocol_example() {
    StatusMessage m;
    m.tsMs = kTs;
    m.event = StatusEvent::Boot;
    m.fw = "0.1.0";
    m.hw = "lilygo-t-call-a7670";
    m.batV = 4.12f;
    m.rssiDbm = -69;
    m.uptimeS = 4;
    char buf[256];
    size_t n = encodeStatus(m, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING(
        "{\"v\":1,\"ts\":1760950001000,\"event\":\"boot\",\"fw\":\"0.1.0\",\"hw\":\"lilygo-t-call-a7670\","
        "\"batV\":4.12,\"rssi\":-69,\"uptime\":4}",
        buf);
    TEST_ASSERT_EQUAL(strlen(buf), n);
}

static void test_optional_fields_are_omitted_not_null() {
    StatusMessage m;
    m.tsMs = kTs;
    m.event = StatusEvent::Heartbeat;
    char buf[128];
    TEST_ASSERT_NOT_EQUAL(0, encodeStatus(m, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("{\"v\":1,\"ts\":1760950001000,\"event\":\"heartbeat\"}", buf);
    TEST_ASSERT_NULL(strstr(buf, "null"));
}

static void test_event_names() {
    TEST_ASSERT_EQUAL_STRING("boot", statusEventName(StatusEvent::Boot));
    TEST_ASSERT_EQUAL_STRING("heartbeat", statusEventName(StatusEvent::Heartbeat));
    TEST_ASSERT_EQUAL_STRING("low_battery", statusEventName(StatusEvent::LowBattery));
    TEST_ASSERT_EQUAL_STRING("shutdown", statusEventName(StatusEvent::Shutdown));
}

static void test_reset_reason_is_extra_field() {
    StatusMessage m;
    m.tsMs = kTs;
    m.resetReason = "panic";
    char buf[128];
    TEST_ASSERT_NOT_EQUAL(0, encodeStatus(m, buf, sizeof(buf)));
    TEST_ASSERT_NOT_NULL(strstr(buf, ",\"rst\":\"panic\"}"));
}

static void test_clock_not_set_is_refused() {
    StatusMessage m;
    m.tsMs = 4000;  // millis() since boot, not an epoch
    char buf[128];
    TEST_ASSERT_EQUAL(0, encodeStatus(m, buf, sizeof(buf)));
}

static void test_too_small_buffer_is_refused_not_truncated() {
    StatusMessage m;
    m.tsMs = kTs;
    m.fw = "0.1.0";
    char buf[30];
    TEST_ASSERT_EQUAL(0, encodeStatus(m, buf, sizeof(buf)));
}

static void test_nan_battery_is_refused() {
    StatusMessage m;
    m.tsMs = kTs;
    m.batV = NAN;
    char buf[128];
    TEST_ASSERT_EQUAL(0, encodeStatus(m, buf, sizeof(buf)));
}

static void test_strings_are_escaped() {
    StatusMessage m;
    m.tsMs = kTs;
    m.fw = "0.1.0-\"dev\"";
    char buf[128];
    TEST_ASSERT_NOT_EQUAL(0, encodeStatus(m, buf, sizeof(buf)));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"fw\":\"0.1.0-\\\"dev\\\"\""));
}

static void test_int64_ts_survives() {
    // Far beyond 32 bits: catches ArduinoJson built without 64-bit integer support.
    StatusMessage m;
    m.tsMs = 4102444800123LL;  // 2100-01-01T00:00:00.123Z
    char buf[128];
    TEST_ASSERT_NOT_EQUAL(0, encodeStatus(m, buf, sizeof(buf)));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ts\":4102444800123,"));
}

static Fix sampleFix(uint32_t tsS, bool full) {
    GnssReading r;
    r.hasFix = true;
    r.timeMs = static_cast<int64_t>(tsS) * 1000;
    r.latDeg = -32.928312;
    r.lonDeg = 151.781705;
    if (full) {
        r.altM = 12.3f;
        r.speedKmh = 4.2f;
        r.courseDeg = 270.0f;
        r.sats = 9;
        r.hdop = 0.9f;
    }
    return *fixFromReading(r);
}

static void test_telemetry_matches_protocol_example() {
    Fix fixes[2] = {sampleFix(1760949990, true), sampleFix(1760950000, false)};
    fixes[1].latE7 = -329284010;
    fixes[1].lonE7 = 1517818220;
    TelemetryMessage m;
    m.tsMs = kTs;
    m.seq = 1042;
    m.batV = 3.98f;
    m.rssiDbm = -71;
    char buf[512];
    TEST_ASSERT_NOT_EQUAL(0, encodeTelemetry(m, fixes, 2, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING(
        "{\"v\":1,\"ts\":1760950001000,\"seq\":1042,\"fixes\":["
        "{\"ts\":1760949990000,\"lat\":-32.928312,\"lon\":151.781705,\"alt\":12,\"spd\":4.2,\"crs\":270,"
        "\"sat\":9,\"hdop\":0.9},"
        "{\"ts\":1760950000000,\"lat\":-32.928401,\"lon\":151.781822}],"
        "\"batV\":3.98,\"rssi\":-71}",
        buf);
}

static void test_telemetry_fix_count_limits() {
    static Fix fixes[101];
    for (uint32_t i = 0; i < 101; ++i) fixes[i] = sampleFix(1760950000 + i * 10, true);
    TelemetryMessage m;
    m.tsMs = kTs;
    static char buf[16384];
    TEST_ASSERT_EQUAL(0, encodeTelemetry(m, fixes, 0, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL(0, encodeTelemetry(m, fixes, 101, buf, sizeof(buf)));
    const size_t full = encodeTelemetry(m, fixes, 100, buf, sizeof(buf));
    TEST_ASSERT_NOT_EQUAL(0, full);
    // Documents what 100 full fixes cost against the modem's 10240-byte publish limit.
    printf("100 full fixes: %u bytes\n", static_cast<unsigned>(full));
}

static void test_telemetry_accuracy_field() {
    Fix f = sampleFix(1760950000, false);
    f.accM = 12;
    f.flags |= Fix::kAcc;
    TelemetryMessage m;
    m.tsMs = kTs;
    char buf[256];
    TEST_ASSERT_NOT_EQUAL(0, encodeTelemetry(m, &f, 1, buf, sizeof(buf)));
    TEST_ASSERT_NOT_NULL(strstr(buf, ",\"acc\":12}"));
}

static void test_telemetry_refuses_unset_clock() {
    Fix f = sampleFix(1760950000, false);
    TelemetryMessage m;
    m.tsMs = 5000;
    char buf[256];
    TEST_ASSERT_EQUAL(0, encodeTelemetry(m, &f, 1, buf, sizeof(buf)));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_boot_matches_protocol_example);
    RUN_TEST(test_optional_fields_are_omitted_not_null);
    RUN_TEST(test_event_names);
    RUN_TEST(test_reset_reason_is_extra_field);
    RUN_TEST(test_clock_not_set_is_refused);
    RUN_TEST(test_too_small_buffer_is_refused_not_truncated);
    RUN_TEST(test_nan_battery_is_refused);
    RUN_TEST(test_strings_are_escaped);
    RUN_TEST(test_int64_ts_survives);
    RUN_TEST(test_telemetry_matches_protocol_example);
    RUN_TEST(test_telemetry_fix_count_limits);
    RUN_TEST(test_telemetry_accuracy_field);
    RUN_TEST(test_telemetry_refuses_unset_clock);
    return UNITY_END();
}
