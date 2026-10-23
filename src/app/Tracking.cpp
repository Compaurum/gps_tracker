#include "Tracking.h"

#include <Arduino.h>

#include "platform/Log.h"

namespace app {

static const char* const TAG = "TRACK";

static constexpr uint32_t kStatsIntervalMs = 60000;

Tracking::Tracking(const Deps& deps) : d_(deps), detector_(deps.cfg) {}

void Tracking::sample(uint32_t nowMs) {
    logStats(nowMs);
    auto s = d_.position.tick(nowMs);
    if (!s) return;

    using Change = core::MotionDetector::Change;
    core::MotionDetector::Input in;
    in.nowMs = nowMs;
    in.latDeg = s->fix.latE7 / 1e7;
    in.lonDeg = s->fix.lonE7 / 1e7;
    in.quality = s->accuracy.quality;
    in.errorM = s->accuracy.errorM;
    in.speedKmh = s->speedKmh;
    const auto change = detector_.update(in);
    if (change == Change::BecameMoving) {
        LOGI(TAG, "moving: telemetry every %lu s", static_cast<unsigned long>(d_.cfg.publishIntervalMs / 1000));
    } else if (change == Change::BecameStationary) {
        LOGI(TAG, "stationary: sending the last %u fixes, then heartbeat only",
             static_cast<unsigned>(d_.outbox.size() + 1));
        flushRequested_ = true;
    }

    // Queue while moving, plus the first fix after boot (where the bike is) and the fix that
    // ends a ride (where it was parked). Parked fixes are not sent: heartbeat instead.
    // Poor fixes are never sent: a position that may be off by hundreds of metres misleads.
    if (s->accuracy.quality == core::FixQuality::Poor) return;
    const bool queue = detector_.moving() || !firstFixQueued_ || change == Change::BecameStationary;
    if (queue) {
        d_.outbox.push(s->fix);
        firstFixQueued_ = true;
    }
}

void Tracking::report(uint32_t nowMs) {
    core::ReportState st;
    st.nowMs = nowMs;
    st.queuedFixes = d_.outbox.size();
    st.attemptedBefore = attemptedBefore_;
    st.lastAttemptMs = lastAttemptMs_;
    st.lastAttemptFailed = lastAttemptFailed_;
    st.lastDeliveredMs = d_.uplink.lastDeliveredMs();
    st.flushRequested = flushRequested_;

    switch (core::planReport(d_.cfg, st)) {
        case core::ReportAction::None:
            return;
        case core::ReportAction::Telemetry: {
            auto res = d_.uplink.sendTelemetry(d_.outbox, d_.cfg.maxFixesPerMessage, d_.modem.rssiDbm());
            lastAttemptFailed_ = res.result != services::PublishResult::Delivered;
            if (!lastAttemptFailed_ && d_.outbox.empty()) flushRequested_ = false;
            break;
        }
        case core::ReportAction::Heartbeat: {
            core::StatusMessage hb;
            hb.event = core::StatusEvent::Heartbeat;
            hb.rssiDbm = d_.modem.rssiDbm();
            hb.uptimeS = millis() / 1000;
            lastAttemptFailed_ = d_.uplink.sendStatus(hb) != services::PublishResult::Delivered;
            break;
        }
    }
    attemptedBefore_ = true;
    lastAttemptMs_ = millis();
}

void Tracking::onModemReset() {
    d_.position.onModemReset();
}

void Tracking::onLinkUp() {
    if (assistTried_ || d_.position.hasLock()) return;
    assistTried_ = true;
    d_.position.requestAssist();
}

void Tracking::logStats(uint32_t nowMs) {
    if (nowMs - lastStatsMs_ < kStatsIntervalMs) return;
    lastStatsMs_ = nowMs;
    LOGI(TAG, "%s, %s, %u/%u fixes queued, %lu dropped", d_.position.hasLock() ? "gnss lock" : "no gnss lock",
         detector_.moving() ? "moving" : "stationary", static_cast<unsigned>(d_.outbox.size()),
         static_cast<unsigned>(d_.outbox.capacity()), static_cast<unsigned long>(d_.outbox.dropped()));
}

}  // namespace app
