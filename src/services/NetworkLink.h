#pragma once

#include "drivers/A7670Modem.h"

namespace services {

// Cellular link: SIM, registration, data bearer. Retry policy lives in the caller (Lifecycle).
class NetworkLink {
public:
    NetworkLink(drivers::A7670Modem& modem, const char* apn);

    bool bringUp();

private:
    drivers::A7670Modem& modem_;
    const char* apn_;
};

}  // namespace services
