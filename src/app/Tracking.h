#pragma once

#include <stdint.h>

#include "BatchPlanner.h"
#include "Config.h"
#include "FixOutbox.h"
#include "MotionDetector.h"
#include "drivers/A7670Modem.h"
#include "services/PositionService.h"
#include "services/Uplink.h"

namespace app {

// The data flow of P2 (docs/ARCHITECTURE.md §5): GNSS sample → motion → outbox → telemetry or
// heartbeat. Sampling runs whenever the modem is up, also while the link is down, so fixes
// collect in the outbox and go out oldest first after reconnecting (store and forward).
class Tracking {
public:
    struct Deps {
        services::PositionService& position;
        services::Uplink& uplink;
        drivers::A7670Modem& modem;
        core::FixOutbox& outbox;
        const core::TrackerConfig& cfg;
    };

    explicit Tracking(const Deps& deps);

    // Every loop pass while the modem is up, online or not.
    void sample(uint32_t nowMs);
    // Only while MQTT is connected: publishes what planReport() asks for.
    void report(uint32_t nowMs);
    // The modem was reset; the GNSS receiver inside it is off now.
    void onModemReset();
    // MQTT just came up (data bearer available): once per boot, fetch assisted GPS data if
    // there is no lock yet.
    void onLinkUp();

private:
    void logStats(uint32_t nowMs);

    Deps d_;
    core::MotionDetector detector_;
    bool assistTried_ = false;
    bool firstFixQueued_ = false;
    bool flushRequested_ = false;
    bool attemptedBefore_ = false;
    uint32_t lastAttemptMs_ = 0;
    bool lastAttemptFailed_ = false;
    uint32_t lastStatsMs_ = 0;
};

}  // namespace app
