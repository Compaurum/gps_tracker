#pragma once

#include <optional>
#include <stdint.h>

#include "AtChannel.h"

namespace drivers {

// SIMCom A7670 control: power, SIM, registration, data bearer, clock, certificate store.
// MQTT lives in services/MqttSession on top of the same AtChannel.
class A7670Modem : public UrcListener {
public:
    explicit A7670Modem(AtChannel& at);

    // Brings the modem to a state where it answers AT. Re-uses a modem that is already on
    // (ESP32-only reset), otherwise runs the reset + PWRKEY sequence. Applies base settings.
    bool powerOn();
    // Full modem reset (AT+CRESET), for recovery when it is stuck. powerOn() must follow.
    void reset();

    bool waitSimReady(uint32_t timeoutMs);
    // Waits for LTE (CEREG) or 2G (CREG) registration, home or roaming.
    bool waitRegistered(uint32_t timeoutMs);
    // Defines the PDP context (if `apn` is non-empty) and makes sure it is active.
    bool activateData(const char* apn);

    std::optional<int> rssiDbm();
    // Sets the modem RTC from NTP (timezone 0) and returns it as UTC epoch ms.
    // nullopt if NTP failed: the RTC is then not trusted, whatever it holds.
    std::optional<int64_t> ntpTimeMs(const char* server);

    // One AT+CCERTLIST; present[i] tells whether names[i] is stored. False if the list failed.
    bool findCertificates(const char* const* names, bool* present, size_t n);
    bool hasCertificate(const char* name);
    bool uploadCertificate(const char* name, const char* pem);

    void logIdentity();

    AtChannel& at() { return at_; }

    bool onUrc(const char* line) override;

private:
    bool probe(uint32_t attempts);

    AtChannel& at_;
};

}  // namespace drivers
