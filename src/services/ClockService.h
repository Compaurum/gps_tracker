#pragma once

#include <stdint.h>

#include "drivers/A7670Modem.h"

namespace services {

// UTC wall clock in epoch milliseconds, anchored to millis(). Unset until a trusted source
// sets it, and nothing may be published until then: the backend silently drops messages with
// an implausible ts. Trusted sources: NTP (when the link comes up) and GNSS (P2). The modem's
// own RTC is never trusted on its own; after power-up it reads "70/01/01".
class ClockService {
public:
    explicit ClockService(drivers::A7670Modem& modem);

    bool syncFromNtp(const char* server);
    // Sets the clock from any trusted source (GNSS in P2).
    void set(int64_t epochMs, const char* source);

    bool isSet() const { return set_; }
    int64_t nowMs() const;

private:
    drivers::A7670Modem& modem_;
    bool set_ = false;
    int64_t anchorEpochMs_ = 0;
    uint32_t anchorMillis_ = 0;
};

}  // namespace services
