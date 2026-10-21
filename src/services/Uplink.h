#pragma once

#include "ClockService.h"
#include "MqttSession.h"
#include "ProtocolTypes.h"

namespace services {

// Encodes protocol messages and publishes them on this device's own topics.
// Telemetry batches from the fix outbox join in P2.
class Uplink {
public:
    Uplink(MqttSession& mqtt, ClockService& clock, const char* deviceId);

    // Builds the topic names. False if the device ID is not a valid thing name.
    bool begin();

    const char* cmdTopic() const { return cmdTopic_; }

    // Stamps `msg.tsMs` with the current time and publishes it. Refuses (Failed) while the
    // clock is not set, because the backend would drop the message without telling anyone.
    PublishResult sendStatus(core::StatusMessage msg);

private:
    MqttSession& mqtt_;
    ClockService& clock_;
    const char* deviceId_;
    char statusTopic_[96] = {};
    char cmdTopic_[96] = {};
};

}  // namespace services
