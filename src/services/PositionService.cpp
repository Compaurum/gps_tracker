#include "PositionService.h"

#include "platform/Log.h"

namespace services {

static const char* const TAG = "POS";

static constexpr uint32_t kPowerRetryMs = 60000;
// Re-anchor the wall clock to GNSS time this often; millis() drifts a few seconds a day.
static constexpr uint32_t kClockResyncMs = 10UL * 60 * 1000;
// Single empty readings happen with a weak signal; only this many in a row count as a lost lock.
static constexpr uint8_t kNoFixSamplesForLost = 3;
// Unreadable this many samples in a row: re-power and re-configure the receiver.
static constexpr uint8_t kSilentSamplesForRestart = 3;

PositionService::PositionService(drivers::GnssSource& gnss, ClockService& clock, const core::TrackerConfig& cfg)
    : gnss_(gnss), clock_(clock), cfg_(cfg) {}

void PositionService::onModemReset() {
    gnss_.markOff();
    locked_ = false;
    triedPower_ = false;
}

void PositionService::requestAssist() {
    if (!gnss_.isOn() || locked_) return;
    gnss_.assist();
}

void PositionService::ensurePowered(uint32_t nowMs) {
    if (gnss_.isOn()) return;
    if (triedPower_ && nowMs - lastPowerTryMs_ < kPowerRetryMs) return;
    triedPower_ = true;
    lastPowerTryMs_ = nowMs;
    if (!gnss_.powerOn()) {
        LOGW(TAG, "receiver did not start, retry in %lu s", static_cast<unsigned long>(kPowerRetryMs / 1000));
    }
}

void PositionService::syncClock(const core::GnssReading& r, uint32_t nowMs) {
    // Only a locked receiver's time is trusted (its clock is junk before the first lock).
    if (clockFromGnss_ && nowMs - lastClockSyncMs_ < kClockResyncMs) return;
    clock_.set(r.timeMs, "gnss");
    clockFromGnss_ = true;
    lastClockSyncMs_ = nowMs;
}

void PositionService::noFix() {
    if (noFixInRow_ < 255) ++noFixInRow_;
    if (locked_ && noFixInRow_ >= kNoFixSamplesForLost) {
        LOGI(TAG, "lock lost (%u samples without a position)", static_cast<unsigned>(noFixInRow_));
        locked_ = false;
        lastQuality_ = core::FixQuality::Poor;
    }
}

std::optional<PositionService::Sample> PositionService::tick(uint32_t nowMs) {
    ensurePowered(nowMs);
    if (!gnss_.isOn()) return std::nullopt;
    if (sampledBefore_ && nowMs - lastSampleMs_ < cfg_.sampleIntervalMs) return std::nullopt;
    sampledBefore_ = true;
    lastSampleMs_ = nowMs;

    auto reading = gnss_.read();
    if (!reading) {
        if (++silentInRow_ >= kSilentSamplesForRestart) {
            LOGW(TAG, "receiver silent for %u samples, restarting it", static_cast<unsigned>(silentInRow_));
            silentInRow_ = 0;
            gnss_.markOff();
            triedPower_ = false;
        }
        noFix();
        return std::nullopt;
    }
    silentInRow_ = 0;

    auto fix = core::fixFromReading(*reading);
    if (!fix) {
        if (reading->hasFix) {
            LOGW(TAG, "reading rejected: lat %.6f lon %.6f t %ld s", reading->latDeg, reading->lonDeg,
                 static_cast<long>(reading->timeMs / 1000));
        }
        noFix();
        return std::nullopt;
    }
    noFixInRow_ = 0;

    const core::Accuracy acc = core::assessAccuracy(*reading, cfg_);
    if (!locked_) {
        LOGI(TAG, "lock: %.6f, %.6f, %d sats", reading->latDeg, reading->lonDeg, reading->sats.value_or(0));
        locked_ = true;
    }
    if (acc.quality != lastQuality_) {
        LOGI(TAG, "quality %s: ~%.0f m (%s), hdop %.1f, %d sats", core::fixQualityName(acc.quality), acc.errorM,
             acc.fromReceiver ? "receiver estimate" : "hdop guess", reading->hdop.value_or(0.0f),
             reading->sats.value_or(0));
        lastQuality_ = acc.quality;
    }
    syncClock(*reading, nowMs);

    Sample s{*fix, acc, std::nullopt};
    if (acc.quality == core::FixQuality::Good) {
        s.speedKmh = reading->speedKmh;
    } else {
        s.fix.clearMotion();
    }
    return s;
}

}  // namespace services
