#include <unity.h>

#include <cmath>
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
    return UNITY_END();
}
