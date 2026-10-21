#include "ClockService.h"

#include <Arduino.h>

#include "ClockMath.h"
#include "platform/Log.h"

namespace services {

static const char* const TAG = "CLOCK";

ClockService::ClockService(drivers::A7670Modem& modem)
    : modem_(modem) {}

bool ClockService::syncFromNtp(const char* server) {
    auto ms = modem_.ntpTimeMs(server);
    if (!ms) return false;
    // A successful NTP answer before 2020 means something upstream is broken; don't use it.
    if (*ms < core::kMinPlausibleEpochMs) {
        LOGE(TAG, "NTP returned %lu s, ignored", static_cast<unsigned long>(*ms / 1000));
        return false;
    }
    set(*ms, "ntp");
    return true;
}

void ClockService::set(int64_t epochMs, const char* source) {
    const int64_t before = set_ ? nowMs() : 0;
    anchorEpochMs_ = epochMs;
    anchorMillis_ = millis();
    if (set_) {
        LOGI(TAG, "resynced from %s, drift %ld ms", source, static_cast<long>(epochMs - before));
    } else {
        LOGI(TAG, "set from %s: %lu s", source, static_cast<unsigned long>(epochMs / 1000));
    }
    set_ = true;
}

int64_t ClockService::nowMs() const {
    // Unsigned subtraction stays correct across the 49-day millis() wrap.
    return anchorEpochMs_ + static_cast<uint32_t>(millis() - anchorMillis_);
}

}  // namespace services
