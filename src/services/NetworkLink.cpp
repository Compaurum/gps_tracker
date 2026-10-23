#include "NetworkLink.h"

#include "platform/Log.h"

namespace services {

static const char* const TAG = "NET";

static constexpr uint32_t kCheckIntervalMs = 1000;
static constexpr uint32_t kSimTimeoutMs = 20000;
// A cold first registration on a new SIM/cell can take a couple of minutes.
static constexpr uint32_t kRegisterTimeoutMs = 180000;

NetworkLink::NetworkLink(drivers::A7670Modem& modem, const char* apn) : modem_(modem), apn_(apn) {}

void NetworkLink::begin(uint32_t nowMs) {
    step_ = Step::Sim;
    startMs_ = stepStartMs_ = nowMs;
    checkedOnce_ = false;
    lastStat_ = -1;
}

NetworkLink::Status NetworkLink::poll(uint32_t nowMs) {
    if (checkedOnce_ && nowMs - lastCheckMs_ < kCheckIntervalMs) return Status::Pending;
    checkedOnce_ = true;
    lastCheckMs_ = nowMs;

    if (step_ == Step::Sim) {
        if (!modem_.isSimReady()) {
            if (nowMs - stepStartMs_ < kSimTimeoutMs) return Status::Pending;
            LOGE(TAG, "SIM not ready after %lu ms", static_cast<unsigned long>(kSimTimeoutMs));
            return Status::Failed;
        }
        step_ = Step::Register;
        stepStartMs_ = nowMs;
    }

    int stat = -1;
    const char* rat = "";
    if (!modem_.isRegistered(&stat, &rat)) {
        if (stat != lastStat_) {
            LOGI(TAG, "registration status %d", stat);
            lastStat_ = stat;
        }
        if (nowMs - stepStartMs_ < kRegisterTimeoutMs) return Status::Pending;
        LOGE(TAG, "not registered after %lu ms", static_cast<unsigned long>(kRegisterTimeoutMs));
        return Status::Failed;
    }
    LOGI(TAG, "registered on %s (%s) after %lu ms", rat, stat == 5 ? "roaming" : "home",
         static_cast<unsigned long>(nowMs - startMs_));

    if (!modem_.activateData(apn_)) {
        LOGE(TAG, "data bearer not active");
        return Status::Failed;
    }
    auto rssi = modem_.rssiDbm();
    if (rssi) {
        LOGI(TAG, "up, rssi %d dBm", *rssi);
    } else {
        LOGI(TAG, "up, rssi unknown");
    }
    return Status::Up;
}

}  // namespace services
