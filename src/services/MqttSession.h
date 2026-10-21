#pragma once

#include <stddef.h>
#include <stdint.h>

#include "drivers/AtChannel.h"

namespace services {

struct MqttConfig {
    const char* host;
    uint16_t port;
    const char* clientId;  // must equal the IoT thing name
    // File names in the modem's certificate store.
    const char* caCertFile;
    const char* clientCertFile;
    const char* clientKeyFile;
    uint16_t keepaliveS;
};

enum class PublishResult { Delivered, Failed, LinkLost };

const char* publishResultName(PublishResult r);

// MQTT 3.1.1 over TLS 1.2 using the A7670's built-in client (AT+CMQTT*), client index 0,
// SSL context 0. QoS 1 only. publish() returns Delivered only once the broker's PUBACK has
// arrived (the "+CMQTTPUB: 0,0" URC), which is the protocol's definition of delivered.
class MqttSession : public drivers::UrcListener {
public:
    using MessageFn = void (*)(const char* topic, const uint8_t* payload, size_t len, void* ctx);

    // Max inbound payload kept; longer cmd messages are read and dropped.
    static constexpr size_t kMaxRxPayload = 1024;
    static constexpr size_t kMaxRxTopic = 128;

    MqttSession(drivers::AtChannel& at, const MqttConfig& cfg);

    void setMessageHandler(MessageFn fn, void* ctx);

    bool connect();
    bool subscribe(const char* topic);
    PublishResult publish(const char* topic, const char* payload, uint32_t timeoutMs);
    void disconnect();

    bool isConnected() const { return connected_; }

    bool onUrc(const char* line) override;

private:
    void releaseClient();
    bool configureTls();
    bool sendTopicAndPayload(const char* topic, const char* payload);
    void receiveMessage(const char* startLine);

    drivers::AtChannel& at_;
    MqttConfig cfg_;
    bool connected_ = false;
    MessageFn onMessage_ = nullptr;
    void* onMessageCtx_ = nullptr;
    char rxTopic_[kMaxRxTopic] = {};
    uint8_t rxPayload_[kMaxRxPayload + 1] = {};
};

}  // namespace services
