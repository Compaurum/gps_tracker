#pragma once

#include <optional>
#include <stdint.h>

#include "ClockService.h"
#include "Config.h"
#include "Fix.h"
#include "FixQuality.h"
#include "drivers/gnss/GnssSource.h"

namespace services {

// Samples the GNSS receiver every sampleIntervalMs, rates each reading (FixQuality.h), packs it
// into a protocol fix, and keeps the wall clock in step with GNSS time. Re-powers the receiver
// if it went away.
class PositionService {
public:
    struct Sample {
        core::Fix fix;                  // speed and course already stripped unless quality is Good
        core::Accuracy accuracy;
        std::optional<float> speedKmh;  // only for Good fixes
    };

    PositionService(drivers::GnssSource& gnss, ClockService& clock, const core::TrackerConfig& cfg);

    // A new locked sample, when one is due. Poor ones are returned too (callers decide).
    std::optional<Sample> tick(uint32_t nowMs);

    bool hasLock() const { return locked_; }
    // The modem was reset: the receiver is off and its lock is gone.
    void onModemReset();
    // Assisted GPS over the data link; only worth it while there is no lock yet.
    void requestAssist();

private:
    void ensurePowered(uint32_t nowMs);
    void syncClock(const core::GnssReading& r, uint32_t nowMs);
    void noFix();

    drivers::GnssSource& gnss_;
    ClockService& clock_;
    const core::TrackerConfig& cfg_;
    bool sampledBefore_ = false;
    uint32_t lastSampleMs_ = 0;
    uint32_t lastPowerTryMs_ = 0;
    bool triedPower_ = false;
    bool locked_ = false;
    uint8_t noFixInRow_ = 0;
    uint8_t silentInRow_ = 0;
    core::FixQuality lastQuality_ = core::FixQuality::Poor;
    bool clockFromGnss_ = false;
    uint32_t lastClockSyncMs_ = 0;
};

}  // namespace services
