#include "Uplink.h"

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
    if (r != PublishResult::Delivered) {
        LOGW(TAG, "status %s: %s", core::statusEventName(msg.event), publishResultName(r));
    }
    return r;
}

}  // namespace services
