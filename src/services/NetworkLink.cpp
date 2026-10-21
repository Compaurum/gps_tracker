#include "NetworkLink.h"

#include "platform/Log.h"

namespace services {

static const char* const TAG = "NET";

// A cold first registration on a new SIM/cell can take a couple of minutes.
static constexpr uint32_t kSimTimeoutMs = 20000;
static constexpr uint32_t kRegisterTimeoutMs = 180000;

NetworkLink::NetworkLink(drivers::A7670Modem& modem, const char* apn) : modem_(modem), apn_(apn) {}

bool NetworkLink::bringUp() {
    if (!modem_.waitSimReady(kSimTimeoutMs)) return false;
    if (!modem_.waitRegistered(kRegisterTimeoutMs)) return false;
    if (!modem_.activateData(apn_)) {
        LOGE(TAG, "data bearer not active");
        return false;
    }
    auto rssi = modem_.rssiDbm();
    if (rssi) {
        LOGI(TAG, "up, rssi %d dBm", *rssi);
    } else {
        LOGI(TAG, "up, rssi unknown");
    }
    return true;
}

}  // namespace services
