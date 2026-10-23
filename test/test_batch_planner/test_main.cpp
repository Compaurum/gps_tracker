#include <unity.h>

#include "BatchPlanner.h"

using namespace core;

void setUp() {}
void tearDown() {}

static const TrackerConfig kCfg;

static ReportState base() {
    ReportState s;
    s.nowMs = 1000000;
    s.attemptedBefore = true;
    s.lastAttemptMs = s.nowMs - 1000;
    s.lastDeliveredMs = s.nowMs - 1000;
    return s;
}

static void test_nothing_to_do() {
    TEST_ASSERT_TRUE(planReport(kCfg, base()) == ReportAction::None);
}

static void test_first_fix_after_boot_goes_out_at_once() {
    ReportState s = base();
    s.attemptedBefore = false;
    s.queuedFixes = 1;
    TEST_ASSERT_TRUE(planReport(kCfg, s) == ReportAction::Telemetry);
}

static void test_waits_for_publish_interval() {
    ReportState s = base();
    s.queuedFixes = 5;
    TEST_ASSERT_TRUE(planReport(kCfg, s) == ReportAction::None);
    s.lastAttemptMs = s.nowMs - kCfg.publishIntervalMs;
    TEST_ASSERT_TRUE(planReport(kCfg, s) == ReportAction::Telemetry);
}

static void test_backlog_drains_without_waiting() {
    ReportState s = base();
    s.queuedFixes = 250;
    TEST_ASSERT_TRUE(planReport(kCfg, s) == ReportAction::Telemetry);
}

static void test_flush_sends_final_fixes() {
    ReportState s = base();
    s.queuedFixes = 3;
    s.flushRequested = true;
    TEST_ASSERT_TRUE(planReport(kCfg, s) == ReportAction::Telemetry);
}

static void test_heartbeat_after_quiet_period() {
    ReportState s = base();
    s.lastAttemptMs = s.nowMs - kCfg.heartbeatIntervalMs;
    s.lastDeliveredMs = s.nowMs - kCfg.heartbeatIntervalMs + 1;
    TEST_ASSERT_TRUE(planReport(kCfg, s) == ReportAction::None);
    s.lastDeliveredMs = s.nowMs - kCfg.heartbeatIntervalMs;
    TEST_ASSERT_TRUE(planReport(kCfg, s) == ReportAction::Heartbeat);
}

static void test_telemetry_wins_over_heartbeat() {
    ReportState s = base();
    s.queuedFixes = 2;
    s.lastAttemptMs = s.nowMs - kCfg.heartbeatIntervalMs;
    s.lastDeliveredMs = s.nowMs - kCfg.heartbeatIntervalMs;
    TEST_ASSERT_TRUE(planReport(kCfg, s) == ReportAction::Telemetry);
}

static void test_failure_backs_off_even_with_backlog() {
    ReportState s = base();
    s.queuedFixes = 500;
    s.lastAttemptFailed = true;
    s.lastAttemptMs = s.nowMs - kCfg.retryAfterFailMs + 1;
    TEST_ASSERT_TRUE(planReport(kCfg, s) == ReportAction::None);
    s.lastAttemptMs = s.nowMs - kCfg.retryAfterFailMs;
    TEST_ASSERT_TRUE(planReport(kCfg, s) == ReportAction::Telemetry);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_nothing_to_do);
    RUN_TEST(test_first_fix_after_boot_goes_out_at_once);
    RUN_TEST(test_waits_for_publish_interval);
    RUN_TEST(test_backlog_drains_without_waiting);
    RUN_TEST(test_flush_sends_final_fixes);
    RUN_TEST(test_heartbeat_after_quiet_period);
    RUN_TEST(test_telemetry_wins_over_heartbeat);
    RUN_TEST(test_failure_backs_off_even_with_backlog);
    return UNITY_END();
}
