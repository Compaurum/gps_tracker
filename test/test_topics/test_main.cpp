#include <unity.h>

#include <string>

#include "Topics.h"

using namespace core;

void setUp() {}
void tearDown() {}

static void test_valid_ids() {
    TEST_ASSERT_TRUE(isValidDeviceId("kawasaki-z400"));
    TEST_ASSERT_TRUE(isValidDeviceId("tracker_01"));
    TEST_ASSERT_TRUE(isValidDeviceId(std::string(64, 'a').c_str()));
}

static void test_invalid_ids() {
    TEST_ASSERT_FALSE(isValidDeviceId(""));
    TEST_ASSERT_FALSE(isValidDeviceId(nullptr));
    TEST_ASSERT_FALSE(isValidDeviceId(std::string(65, 'a').c_str()));
    TEST_ASSERT_FALSE(isValidDeviceId("tracker/01"));  // would escape into another topic
    TEST_ASSERT_FALSE(isValidDeviceId("tracker+"));
    TEST_ASSERT_FALSE(isValidDeviceId("CHANGE ME"));
}

static void test_format() {
    char buf[96];
    TEST_ASSERT_TRUE(formatTopic(buf, sizeof(buf), "kawasaki-z400", Topic::Status));
    TEST_ASSERT_EQUAL_STRING("trackers/kawasaki-z400/status", buf);
    TEST_ASSERT_TRUE(formatTopic(buf, sizeof(buf), "kawasaki-z400", Topic::Telemetry));
    TEST_ASSERT_EQUAL_STRING("trackers/kawasaki-z400/telemetry", buf);
    TEST_ASSERT_TRUE(formatTopic(buf, sizeof(buf), "kawasaki-z400", Topic::Cmd));
    TEST_ASSERT_EQUAL_STRING("trackers/kawasaki-z400/cmd", buf);
}

static void test_format_refuses_small_buffer_and_bad_id() {
    char buf[10];
    TEST_ASSERT_FALSE(formatTopic(buf, sizeof(buf), "kawasaki-z400", Topic::Status));
    char big[96];
    TEST_ASSERT_FALSE(formatTopic(big, sizeof(big), "a/b", Topic::Status));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_valid_ids);
    RUN_TEST(test_invalid_ids);
    RUN_TEST(test_format);
    RUN_TEST(test_format_refuses_small_buffer_and_bad_id);
    return UNITY_END();
}
