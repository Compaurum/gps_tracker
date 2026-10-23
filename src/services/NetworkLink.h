#pragma once

#include <stdint.h>

#include "drivers/A7670Modem.h"

namespace services {

// Cellular link: SIM, registration, data bearer. Polled rather than blocking, so GNSS sampling
// carries on while the modem searches for a network. Retry policy lives in the caller (Lifecycle).
class NetworkLink {
public:
    enum class Status { Pending, Up, Failed };

    NetworkLink(drivers::A7670Modem& modem, const char* apn);

    // Starts a new bring-up attempt.
    void begin(uint32_t nowMs);
    // Advances the attempt by at most one check per second.
    Status poll(uint32_t nowMs);

private:
    enum class Step { Sim, Register };

    drivers::A7670Modem& modem_;
    const char* apn_;
    Step step_ = Step::Sim;
    uint32_t startMs_ = 0;
    uint32_t stepStartMs_ = 0;
    uint32_t lastCheckMs_ = 0;
    bool checkedOnce_ = false;
    int lastStat_ = -1;
};

}  // namespace services
