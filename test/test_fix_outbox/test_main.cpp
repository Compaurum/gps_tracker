#include <unity.h>

#include "FixOutbox.h"

using namespace core;

void setUp() {}
void tearDown() {}

static Fix fixAt(uint32_t s) {
    Fix f;
    f.tsS = s;
    return f;
}

static void test_fifo_and_ack() {
    Fix storage[4];
    FixOutbox box(storage, 4);
    for (uint32_t s = 1; s <= 3; ++s) box.push(fixAt(s));
    Fix out[4];
    uint32_t id = 0;
    TEST_ASSERT_EQUAL(2, box.peekOldest(out, 2, &id));
    TEST_ASSERT_EQUAL_UINT32(1, out[0].tsS);
    TEST_ASSERT_EQUAL_UINT32(2, out[1].tsS);
    box.ack(id, 2);
    TEST_ASSERT_EQUAL(1, box.size());
    TEST_ASSERT_EQUAL(1, box.peekOldest(out, 4, &id));
    TEST_ASSERT_EQUAL_UINT32(3, out[0].tsS);
}

static void test_failed_publish_keeps_everything() {
    Fix storage[4];
    FixOutbox box(storage, 4);
    box.push(fixAt(1));
    box.push(fixAt(2));
    Fix out[4];
    uint32_t id = 0;
    box.peekOldest(out, 4, &id);
    // no ack: publish failed
    TEST_ASSERT_EQUAL(2, box.size());
    TEST_ASSERT_EQUAL(2, box.peekOldest(out, 4, &id));
    TEST_ASSERT_EQUAL_UINT32(1, out[0].tsS);
}

static void test_full_drops_oldest_and_wraps() {
    Fix storage[3];
    FixOutbox box(storage, 3);
    for (uint32_t s = 1; s <= 5; ++s) box.push(fixAt(s));
    TEST_ASSERT_EQUAL(3, box.size());
    TEST_ASSERT_EQUAL_UINT32(2, box.dropped());
    Fix out[3];
    uint32_t id = 0;
    TEST_ASSERT_EQUAL(3, box.peekOldest(out, 3, &id));
    TEST_ASSERT_EQUAL_UINT32(3, out[0].tsS);
    TEST_ASSERT_EQUAL_UINT32(5, out[2].tsS);
}

static void test_ack_after_drop_removes_only_sent_fixes() {
    Fix storage[3];
    FixOutbox box(storage, 3);
    for (uint32_t s = 1; s <= 3; ++s) box.push(fixAt(s));
    Fix out[3];
    uint32_t id = 0;
    box.peekOldest(out, 2, &id);  // sending fixes 1 and 2
    box.push(fixAt(4));           // full: fix 1 dropped meanwhile
    box.ack(id, 2);               // only fix 2 is still there to remove
    TEST_ASSERT_EQUAL(2, box.size());
    box.peekOldest(out, 3, &id);
    TEST_ASSERT_EQUAL_UINT32(3, out[0].tsS);
    TEST_ASSERT_EQUAL_UINT32(4, out[1].tsS);
}

static void test_stale_ack_is_ignored() {
    Fix storage[2];
    FixOutbox box(storage, 2);
    box.push(fixAt(1));
    Fix out[2];
    uint32_t id = 0;
    box.peekOldest(out, 1, &id);
    box.push(fixAt(2));
    box.push(fixAt(3));  // fix 1 dropped
    box.push(fixAt(4));  // fix 2 dropped
    box.ack(id, 1);      // acks fix 1, long gone
    TEST_ASSERT_EQUAL(2, box.size());
}

static void test_zero_capacity_is_safe() {
    FixOutbox box(nullptr, 10);
    box.push(fixAt(1));
    TEST_ASSERT_EQUAL(0, box.size());
    TEST_ASSERT_EQUAL_UINT32(1, box.dropped());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_fifo_and_ack);
    RUN_TEST(test_failed_publish_keeps_everything);
    RUN_TEST(test_full_drops_oldest_and_wraps);
    RUN_TEST(test_ack_after_drop_removes_only_sent_fixes);
    RUN_TEST(test_stale_ack_is_ignored);
    RUN_TEST(test_zero_capacity_is_safe);
    return UNITY_END();
}
