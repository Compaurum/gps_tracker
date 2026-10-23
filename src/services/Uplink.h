#pragma once

#include <optional>
#include <stddef.h>

#include "ClockService.h"
#include "Fix.h"
#include "FixOutbox.h"
#include "MqttSession.h"
#include "ProtocolTypes.h"

namespace services {

// Encodes protocol messages and publishes them on this device's own topics.
class Uplink {
public:
    // The A7670 accepts at most this many payload bytes per publish (AT+CMQTTPAYLOAD).
    static constexpr size_t kMaxPayload = 10240;
    static constexpr size_t kMaxFixesPerMessage = 100;

    struct TelemetryResult {
        PublishResult result;
        size_t fixesSent;  // removed from the outbox, only when delivered
    };

    Uplink(MqttSession& mqtt, ClockService& clock, const char* deviceId);

    // Builds the topic names. False if the device ID is not a valid thing name.
    bool begin();

    const char* cmdTopic() const { return cmdTopic_; }

    // Stamps `msg.tsMs` with the current time and publishes it. Refuses (Failed) while the
    // clock is not set, because the backend would drop the message without telling anyone.
    PublishResult sendStatus(core::StatusMessage msg);

    // Publishes the oldest queued fixes (up to maxFixes, fewer if they don't fit in one
    // payload) and removes them from `outbox` only once the PUBACK arrives.
    TelemetryResult sendTelemetry(core::FixOutbox& outbox, size_t maxFixes, std::optional<int> rssiDbm);

    bool deliveredBefore() const { return deliveredBefore_; }
    uint32_t lastDeliveredMs() const { return lastDeliveredMs_; }

private:
    void markDelivered();

    MqttSession& mqtt_;
    ClockService& clock_;
    const char* deviceId_;
    char statusTopic_[96] = {};
    char telemetryTopic_[96] = {};
    char cmdTopic_[96] = {};
    uint32_t seq_ = 0;
    bool deliveredBefore_ = false;
    uint32_t lastDeliveredMs_ = 0;
    // Static-size work areas: a full batch and its payload, outside the stack.
    core::Fix batch_[kMaxFixesPerMessage];
    char payload_[kMaxPayload + 1];
};

}  // namespace services
