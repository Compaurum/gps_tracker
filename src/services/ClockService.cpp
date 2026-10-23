#include "ClockService.h"

#include <Arduino.h>

#include "ClockMath.h"
#include "platform/Log.h"

namespace services {

static const char* const TAG = "CLOCK";

ClockService::ClockService(drivers::A7670Modem& modem)
    : modem_(modem) {}

bool ClockService::syncFromNtp(const char* const* servers, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        auto ms = modem_.ntpTimeMs(servers[i]);
        if (!ms) {
            LOGW(TAG, "NTP %s failed", servers[i]);
            continue;
        }
        // A successful NTP answer before 2020 means something upstream is broken; don't use it.
        if (*ms < core::kMinPlausibleEpochMs) {
            LOGE(TAG, "NTP %s returned %lu s, ignored", servers[i], static_cast<unsigned long>(*ms / 1000));
            continue;
        }
        set(*ms, servers[i]);
        return true;
    }
    return false;
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
