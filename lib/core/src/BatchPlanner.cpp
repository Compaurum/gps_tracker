#include "BatchPlanner.h"

namespace core {

ReportAction planReport(const TrackerConfig& cfg, const ReportState& s) {
    // All times are millis(); unsigned subtraction stays correct across its wrap.
    const uint32_t sinceAttempt = s.nowMs - s.lastAttemptMs;
    if (s.attemptedBefore && s.lastAttemptFailed && sinceAttempt < cfg.retryAfterFailMs) return ReportAction::None;

    if (s.queuedFixes >= cfg.maxFixesPerMessage) return ReportAction::Telemetry;
    if (s.queuedFixes > 0 &&
        (s.flushRequested || !s.attemptedBefore || sinceAttempt >= cfg.publishIntervalMs)) {
        return ReportAction::Telemetry;
    }
    if (s.nowMs - s.lastDeliveredMs >= cfg.heartbeatIntervalMs) return ReportAction::Heartbeat;
    return ReportAction::None;
}

}  // namespace core
