#pragma once

#include <cstddef>
#include <cstdint>

#include "Config.h"

namespace core {

enum class ReportAction { None, Telemetry, Heartbeat };

struct ReportState {
    uint32_t nowMs = 0;
    size_t queuedFixes = 0;
    bool attemptedBefore = false;  // any publish since boot
    uint32_t lastAttemptMs = 0;
    bool lastAttemptFailed = false;
    uint32_t lastDeliveredMs = 0;  // boot, telemetry or heartbeat
    bool flushRequested = false;   // e.g. just became stationary: send the final fixes now
};

// What to publish next, if anything. Rules, in order:
//   1. right after a failed publish, wait retryAfterFailMs;
//   2. a full message's worth of fixes is queued (backlog) → telemetry now;
//   3. fixes queued and (flush requested, first publish, or publish interval elapsed) → telemetry;
//   4. nothing delivered for heartbeatIntervalMs → heartbeat.
ReportAction planReport(const TrackerConfig& cfg, const ReportState& s);

}  // namespace core
