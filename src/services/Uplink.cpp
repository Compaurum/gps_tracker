#include "Uplink.h"

#include <Arduino.h>

#include "ProtocolEncoder.h"
#include "Topics.h"
#include "platform/Log.h"

namespace services {

static const char* const TAG = "UPLINK";

static constexpr uint32_t kPublishTimeoutMs = 70000;

Uplink::Uplink(MqttSession& mqtt, ClockService& clock, const char* deviceId)
    : mqtt_(mqtt), clock_(clock), deviceId_(deviceId) {}

bool Uplink::begin() {
    if (!core::formatTopic(statusTopic_, sizeof(statusTopic_), deviceId_, core::Topic::Status) ||
        !core::formatTopic(telemetryTopic_, sizeof(telemetryTopic_), deviceId_, core::Topic::Telemetry) ||
        !core::formatTopic(cmdTopic_, sizeof(cmdTopic_), deviceId_, core::Topic::Cmd)) {
        LOGE(TAG, "invalid device id '%s' (must match [a-zA-Z0-9_-]{1,64})", deviceId_);
        return false;
    }
    return true;
}

PublishResult Uplink::sendStatus(core::StatusMessage msg) {
    if (!clock_.isSet()) {
        LOGW(TAG, "clock not set, %s not sent", core::statusEventName(msg.event));
        return PublishResult::Failed;
    }
    msg.tsMs = clock_.nowMs();
    char payload[256];
    if (core::encodeStatus(msg, payload, sizeof(payload)) == 0) {
        LOGE(TAG, "could not encode %s", core::statusEventName(msg.event));
        return PublishResult::Failed;
    }
    LOGI(TAG, "status %s", payload);
    PublishResult r = mqtt_.publish(statusTopic_, payload, kPublishTimeoutMs);
    if (r == PublishResult::Delivered) {
        markDelivered();
    } else {
        LOGW(TAG, "status %s: %s", core::statusEventName(msg.event), publishResultName(r));
    }
    return r;
}

Uplink::TelemetryResult Uplink::sendTelemetry(core::FixOutbox& outbox, size_t maxFixes, std::optional<int> rssiDbm) {
    if (!clock_.isSet()) {
        LOGW(TAG, "clock not set, telemetry not sent");
        return {PublishResult::Failed, 0};
    }
    if (maxFixes > kMaxFixesPerMessage) maxFixes = kMaxFixesPerMessage;
    uint32_t firstId = 0;
    size_t n = outbox.peekOldest(batch_, maxFixes, &firstId);
    if (n == 0) return {PublishResult::Failed, 0};

    core::TelemetryMessage msg;
    msg.tsMs = clock_.nowMs();
    msg.seq = seq_;
    msg.rssiDbm = rssiDbm;
    // 100 fully populated fixes are ~10.3 KB, just over the modem's limit: shed ~10% until it fits.
    size_t len = 0;
    while (n > 0 && (len = core::encodeTelemetry(msg, batch_, n, payload_, sizeof(payload_))) == 0) {
        n -= n / 10 > 0 ? n / 10 : 1;
    }
    if (len == 0) {
        LOGE(TAG, "could not encode telemetry");
        return {PublishResult::Failed, 0};
    }
    ++seq_;
    LOGI(TAG, "telemetry seq %lu: %u fixes, %u bytes, %u queued", static_cast<unsigned long>(*msg.seq),
         static_cast<unsigned>(n), static_cast<unsigned>(len), static_cast<unsigned>(outbox.size()));
    PublishResult r = mqtt_.publish(telemetryTopic_, payload_, kPublishTimeoutMs);
    if (r != PublishResult::Delivered) {
        LOGW(TAG, "telemetry seq %lu: %s, fixes kept", static_cast<unsigned long>(*msg.seq), publishResultName(r));
        return {r, 0};
    }
    outbox.ack(firstId, n);
    markDelivered();
    return {r, n};
}

void Uplink::markDelivered() {
    deliveredBefore_ = true;
    lastDeliveredMs_ = millis();
}

}  // namespace services
