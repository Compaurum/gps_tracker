#include <unity.h>

#include "AtParse.h"

using namespace core;

void setUp() {}
void tearDown() {}

static void test_parse_ints() {
    int v[3] = {-1, -1, -1};
    TEST_ASSERT_EQUAL(2, parseInts("+CMQTTPUB: 0,0", "+CMQTTPUB:", v, 3));
    TEST_ASSERT_EQUAL(0, v[0]);
    TEST_ASSERT_EQUAL(0, v[1]);
    TEST_ASSERT_EQUAL(3, parseInts("+CMQTTRXSTART: 0,17,2039", "+CMQTTRXSTART:", v, 3));
    TEST_ASSERT_EQUAL(2039, v[2]);
    TEST_ASSERT_EQUAL(0, parseInts("+CSQ: 20,99", "+CMQTTPUB:", v, 3));
    TEST_ASSERT_EQUAL(1, parseInts("+CMQTTCONNECT: 0", "+CMQTTCONNECT:", v, 3));
}

static void test_cclk_utc() {
    auto ms = parseCclk("+CCLK: \"25/10/20,08:46:41+00\"");
    TEST_ASSERT_TRUE(ms.has_value());
    TEST_ASSERT_EQUAL_INT64(1760950001000LL, *ms);
}

static void test_cclk_subtracts_timezone() {
    // Sydney, AEDT = UTC+11 = 44 quarter hours
    auto ms = parseCclk("+CCLK: \"25/10/20,19:46:41+44\"");
    TEST_ASSERT_TRUE(ms.has_value());
    TEST_ASSERT_EQUAL_INT64(1760950001000LL, *ms);
    auto west = parseCclk("+CCLK: \"25/10/20,04:46:41-16\"");
    TEST_ASSERT_TRUE(west.has_value());
    TEST_ASSERT_EQUAL_INT64(1760950001000LL, *west);
}

static void test_cclk_garbage() {
    TEST_ASSERT_FALSE(parseCclk("+CCLK: \"garbage\"").has_value());
    TEST_ASSERT_FALSE(parseCclk("+CSQ: 1,2").has_value());
    TEST_ASSERT_FALSE(parseCclk("+CCLK: \"25/13/20,08:46:41+00\"").has_value());
}

static void test_csq() {
    TEST_ASSERT_EQUAL(-73, *parseCsqDbm("+CSQ: 20,99"));
    TEST_ASSERT_EQUAL(-113, *parseCsqDbm("+CSQ: 0,0"));
    TEST_ASSERT_FALSE(parseCsqDbm("+CSQ: 99,99").has_value());
}

static void test_registration() {
    int stat = -1;
    TEST_ASSERT_TRUE(parseRegistered("+CEREG: 0,1", "+CEREG:", &stat));
    TEST_ASSERT_TRUE(parseRegistered("+CEREG: 0,5", "+CEREG:", &stat));
    TEST_ASSERT_FALSE(parseRegistered("+CEREG: 0,2", "+CEREG:", &stat));
    TEST_ASSERT_EQUAL(2, stat);
    TEST_ASSERT_FALSE(parseRegistered("+CREG: 0,3", "+CEREG:", &stat));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_ints);
    RUN_TEST(test_cclk_utc);
    RUN_TEST(test_cclk_subtracts_timezone);
    RUN_TEST(test_cclk_garbage);
    RUN_TEST(test_csq);
    RUN_TEST(test_registration);
    return UNITY_END();
}
