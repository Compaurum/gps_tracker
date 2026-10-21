#include <unity.h>

#include "Endpoint.h"

using namespace core;

void setUp() {}
void tearDown() {}

static void test_aws_endpoint_is_valid() {
    TEST_ASSERT_TRUE(isValidHostName("a3o3bbnirgemvs-ats.iot.ap-southeast-2.amazonaws.com"));
}

static void test_copy_paste_slips_are_rejected() {
    // provision-device.sh prints "endpoint: <host>:8883"; the port must not end up in mqtt_host.
    TEST_ASSERT_FALSE(isValidHostName("a3o3bbnirgemvs-ats.iot.ap-southeast-2.amazonaws.com:8883"));
    TEST_ASSERT_FALSE(isValidHostName("mqtts://example.com"));
    TEST_ASSERT_FALSE(isValidHostName("example.com/path"));
    TEST_ASSERT_FALSE(isValidHostName(" example.com"));
    TEST_ASSERT_FALSE(isValidHostName("example.com."));
    TEST_ASSERT_FALSE(isValidHostName(""));
    TEST_ASSERT_FALSE(isValidHostName(nullptr));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_aws_endpoint_is_valid);
    RUN_TEST(test_copy_paste_slips_are_rejected);
    return UNITY_END();
}
