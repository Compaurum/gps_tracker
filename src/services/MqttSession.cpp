#include "MqttSession.h"

#include <Arduino.h>
#include <string.h>

#include "AtParse.h"
#include "platform/Log.h"

using drivers::AtResult;

namespace services {

static const char* const TAG = "MQTT";

// Rule for every wait below: outlast the modem's own timeout, so the modem always answers before
// we give up. Otherwise its late OK/ERROR could be read as the answer to the next command.
// The A7670 (A011B07) holds back the OK of network commands until the network has answered.

// Seconds the modem waits for a PUBACK before reporting failure (A7670 accepts 60-180).
static constexpr uint32_t kModemPubTimeoutS = 60;
static constexpr uint32_t kPubCommandTimeoutMs = kModemPubTimeoutS * 1000 + 10000;
// Seconds the modem waits for the broker during a disconnect (A7670 accepts 0-180).
static constexpr uint32_t kModemDiscTimeoutS = 10;
static constexpr uint32_t kDiscTimeoutMs = kModemDiscTimeoutS * 1000 + 10000;
// TCP + TLS handshake; ~6 s at good signal when roaming, much longer on a weak link or 2G.
static constexpr uint32_t kConnectTimeoutMs = 60000;
static constexpr uint32_t kSubscribeTimeoutMs = 30000;
// Local-only commands (no network round trip).
static constexpr uint32_t kLocalTimeoutMs = 10000;
// The modem's own per-publish limit for +CMQTTPAYLOAD.
static constexpr size_t kMaxPayload = 10240;

const char* publishResultName(PublishResult r) {
    switch (r) {
        case PublishResult::Delivered: return "delivered";
        case PublishResult::Failed: return "failed";
        case PublishResult::LinkLost: return "link lost";
    }
    return "?";
}

MqttSession::MqttSession(drivers::AtChannel& at, const MqttConfig& cfg) : at_(at), cfg_(cfg) {
    at_.addListener(this);
}

void MqttSession::setMessageHandler(MessageFn fn, void* ctx) {
    onMessage_ = fn;
    onMessageCtx_ = ctx;
}

void MqttSession::releaseClient() {
    // Tear down whatever a previous run left in the modem; every step may fail harmlessly.
    char line[48];
    if (at_.exec(kDiscTimeoutMs, "+CMQTTDISC=0,%u", static_cast<unsigned>(kModemDiscTimeoutS)) == AtResult::Ok) {
        at_.waitFor("+CMQTTDISC:", line, sizeof(line), kDiscTimeoutMs);
    }
    at_.exec(kLocalTimeoutMs, "+CMQTTREL=0");
    if (at_.exec(kLocalTimeoutMs, "+CMQTTSTOP") == AtResult::Ok) {
        at_.waitFor("+CMQTTSTOP:", line, sizeof(line), kLocalTimeoutMs);
    }
}

bool MqttSession::configureTls() {
    // sslversion 3 = TLS 1.2 only. authmode 2 = verify the server and present a client cert.
    // ignorelocaltime 1: skip certificate validity-date checks against the modem RTC, which may
    // still be unset on a first boot. The server is still verified against the pinned Amazon
    // Root CA 1; the only thing accepted is an expired-but-genuine certificate.
    // SNI is required by AWS IoT's ATS endpoints.
    return at_.exec(2000, "+CSSLCFG=\"sslversion\",0,3") == AtResult::Ok &&
           at_.exec(2000, "+CSSLCFG=\"authmode\",0,2") == AtResult::Ok &&
           at_.exec(2000, "+CSSLCFG=\"cacert\",0,\"%s\"", cfg_.caCertFile) == AtResult::Ok &&
           at_.exec(2000, "+CSSLCFG=\"clientcert\",0,\"%s\"", cfg_.clientCertFile) == AtResult::Ok &&
           at_.exec(2000, "+CSSLCFG=\"clientkey\",0,\"%s\"", cfg_.clientKeyFile) == AtResult::Ok &&
           at_.exec(2000, "+CSSLCFG=\"ignorelocaltime\",0,1") == AtResult::Ok &&
           at_.exec(2000, "+CSSLCFG=\"enableSNI\",0,1") == AtResult::Ok;
}

bool MqttSession::connect() {
    const uint32_t start = millis();
    connected_ = false;
    releaseClient();

    if (!configureTls()) {
        LOGE(TAG, "TLS config failed");
        return false;
    }

    char line[64];
    if (at_.exec(5000, "+CMQTTSTART") != AtResult::Ok) return false;
    if (!at_.waitFor("+CMQTTSTART:", line, sizeof(line), 30000)) {
        LOGE(TAG, "CMQTTSTART: no result");
        return false;
    }
    int code = -1;
    core::parseInts(line, "+CMQTTSTART:", &code, 1);
    if (code != 0 && code != 23) {  // 23 = already started
        LOGE(TAG, "CMQTTSTART failed: %d", code);
        return false;
    }

    // Last argument 1 = TLS server type.
    if (at_.exec(3000, "+CMQTTACCQ=0,\"%s\",1", cfg_.clientId) != AtResult::Ok) return false;
    if (at_.exec(3000, "+CMQTTSSLCFG=0,0") != AtResult::Ok) return false;
    // Protocol level 4 = MQTT 3.1.1 (the modem defaults to 3.1).
    if (at_.exec(3000, "+CMQTTCFG=\"version\",0,4") != AtResult::Ok) return false;

    // Clean session 1: nothing is queued for the device on the broker today.
    // The A7670 (A011B07) holds the OK until the TCP + TLS handshake is done (~6 s when roaming), so
    // the command itself gets the full connect timeout. A quick refusal arrives as
    // "+CMQTTCONNECT: 0,<err>" followed by ERROR; keep the line for the log.
    if (at_.query(kConnectTimeoutMs, "+CMQTTCONNECT:", line, sizeof(line), "+CMQTTCONNECT=0,\"tcp://%s:%u\",%u,1", cfg_.host,
                  cfg_.port, cfg_.keepaliveS) != AtResult::Ok) {
        // Codes are in the SIMCom A76XX MQTT(S) application note.
        LOGE(TAG, "connect refused: %s", line[0] ? line : "no result code");
        return false;
    }
    // Normally the result follows OK; use it directly if it came before.
    if (!line[0] && !at_.waitFor("+CMQTTCONNECT:", line, sizeof(line), kConnectTimeoutMs)) {
        LOGE(TAG, "connect: no result after %lu ms", static_cast<unsigned long>(kConnectTimeoutMs));
        return false;
    }
    int v[2] = {-1, -1};
    if (core::parseInts(line, "+CMQTTCONNECT:", v, 2) != 2 || v[1] != 0) {
        LOGE(TAG, "connect refused: %s", line);
        return false;
    }
    connected_ = true;
    LOGI(TAG, "connected to %s as %s in %lu ms", cfg_.host, cfg_.clientId,
         static_cast<unsigned long>(millis() - start));
    return true;
}

bool MqttSession::subscribe(const char* topic) {
    if (!connected_) return false;
    // QoS 1, per the protocol.
    const unsigned len = static_cast<unsigned>(strlen(topic));
    if (at_.sendData(kSubscribeTimeoutMs, topic, len, "+CMQTTSUB=0,%u,1", len) != AtResult::Ok) {
        return false;
    }
    char line[48];
    if (!at_.waitFor("+CMQTTSUB:", line, sizeof(line), kSubscribeTimeoutMs)) {
        LOGE(TAG, "subscribe %s: no SUBACK", topic);
        return false;
    }
    int v[2] = {-1, -1};
    if (core::parseInts(line, "+CMQTTSUB:", v, 2) != 2 || v[1] != 0) {
        LOGE(TAG, "subscribe %s refused: %s", topic, line);
        return false;
    }
    LOGI(TAG, "subscribed %s", topic);
    return true;
}

bool MqttSession::sendTopicAndPayload(const char* topic, const char* payload) {
    const size_t tlen = strlen(topic);
    const size_t plen = strlen(payload);
    // Both only fill the modem's buffers; no network round trip.
    return at_.sendData(kLocalTimeoutMs, topic, tlen, "+CMQTTTOPIC=0,%u", static_cast<unsigned>(tlen)) ==
               AtResult::Ok &&
           at_.sendData(kLocalTimeoutMs, payload, plen, "+CMQTTPAYLOAD=0,%u", static_cast<unsigned>(plen)) == AtResult::Ok;
}

PublishResult MqttSession::publish(const char* topic, const char* payload, uint32_t timeoutMs) {
    if (!connected_) return PublishResult::LinkLost;
    const size_t plen = strlen(payload);
    if (plen == 0 || plen > kMaxPayload) {
        LOGE(TAG, "payload size %u out of range", static_cast<unsigned>(plen));
        return PublishResult::Failed;
    }
    const uint32_t start = millis();
    if (!sendTopicAndPayload(topic, payload)) return connected_ ? PublishResult::Failed : PublishResult::LinkLost;
    if (at_.exec(kPubCommandTimeoutMs, "+CMQTTPUB=0,1,%u", static_cast<unsigned>(kModemPubTimeoutS)) !=
        AtResult::Ok) {
        return connected_ ? PublishResult::Failed : PublishResult::LinkLost;
    }

    // OK above only means "queued". Delivered = the +CMQTTPUB: 0,<err> URC with err 0.
    char line[48];
    while (millis() - start < timeoutMs) {
        if (at_.waitFor("+CMQTTPUB:", line, sizeof(line), 500)) {
            int v[2] = {-1, -1};
            if (core::parseInts(line, "+CMQTTPUB:", v, 2) == 2 && v[1] == 0) {
                LOGI(TAG, "PUBACK %s (%u bytes) in %lu ms", topic, static_cast<unsigned>(plen),
                     static_cast<unsigned long>(millis() - start));
                return PublishResult::Delivered;
            }
            LOGW(TAG, "publish %s failed: %s", topic, line);
            return connected_ ? PublishResult::Failed : PublishResult::LinkLost;
        }
        // A CONNLOST URC dispatched during the wait ends it early.
        if (!connected_) return PublishResult::LinkLost;
    }
    LOGW(TAG, "no PUBACK for %s after %lu ms", topic, static_cast<unsigned long>(timeoutMs));
    return PublishResult::Failed;
}

void MqttSession::disconnect() {
    releaseClient();
    connected_ = false;
}

bool MqttSession::onUrc(const char* line) {
    if (core::startsWith(line, "+CMQTTCONNLOST:")) {
        // Cause: 1 socket closed passively, 2 socket reset, 3 network closed.
        LOGW(TAG, "connection lost: %s", line);
        connected_ = false;
        return true;
    }
    if (core::startsWith(line, "+CMQTTNONET")) {
        LOGW(TAG, "network gone");
        connected_ = false;
        return true;
    }
    if (core::startsWith(line, "+CMQTTRXSTART:")) {
        receiveMessage(line);
        return true;
    }
    // Results that arrived after the caller stopped waiting.
    if (core::startsWith(line, "+CMQTTPUB:") || core::startsWith(line, "+CMQTTCONNECT:") ||
        core::startsWith(line, "+CMQTTSUB:")) {
        LOGW(TAG, "late result ignored: %s", line);
        return true;
    }
    // Late results of teardown commands.
    if (core::startsWith(line, "+CMQTTSTOP:") || core::startsWith(line, "+CMQTTDISC:") ||
        core::startsWith(line, "+CMQTTSTART:")) {
        LOGD(TAG, "urc: %s", line);
        return true;
    }
    return false;
}

// +CMQTTRXSTART: 0,<topic_len>,<payload_len>
// +CMQTTRXTOPIC: 0,<n>      then n raw bytes
// +CMQTTRXPAYLOAD: 0,<n>    then n raw bytes, repeated for long payloads
// +CMQTTRXEND: 0
void MqttSession::receiveMessage(const char* startLine) {
    int head[3] = {0, 0, 0};
    core::parseInts(startLine, "+CMQTTRXSTART:", head, 3);
    const size_t payloadTotal = head[2] > 0 ? static_cast<size_t>(head[2]) : 0;
    size_t kept = 0;
    size_t received = 0;
    rxTopic_[0] = '\0';

    char line[64];
    for (;;) {
        if (!at_.readLine(line, sizeof(line), 3000)) {
            LOGW(TAG, "rx: incomplete message");
            return;
        }
        int v[2] = {0, 0};
        if (core::parseInts(line, "+CMQTTRXTOPIC:", v, 2) == 2) {
            const size_t n = static_cast<size_t>(v[1]);
            char scratch[kMaxRxTopic];
            const size_t keep = n < sizeof(rxTopic_) - 1 ? n : sizeof(rxTopic_) - 1;
            at_.readRaw(rxTopic_, keep, 3000);
            rxTopic_[keep] = '\0';
            for (size_t rest = n - keep; rest > 0;) {
                const size_t chunk = rest < sizeof(scratch) ? rest : sizeof(scratch);
                if (at_.readRaw(scratch, chunk, 3000) != chunk) break;
                rest -= chunk;
            }
        } else if (core::parseInts(line, "+CMQTTRXPAYLOAD:", v, 2) == 2) {
            size_t n = static_cast<size_t>(v[1]);
            received += n;
            while (n > 0) {
                uint8_t scratch[64];
                const size_t room = kMaxRxPayload - kept;
                if (room > 0) {
                    const size_t chunk = n < room ? n : room;
                    const size_t got = at_.readRaw(rxPayload_ + kept, chunk, 3000);
                    kept += got;
                    n -= got;
                    if (got != chunk) break;
                } else {
                    const size_t chunk = n < sizeof(scratch) ? n : sizeof(scratch);
                    const size_t got = at_.readRaw(scratch, chunk, 3000);
                    n -= got;
                    if (got != chunk) break;
                }
            }
        } else if (core::startsWith(line, "+CMQTTRXEND:")) {
            break;
        } else {
            LOGW(TAG, "rx: unexpected line %s", line);
        }
    }
    rxPayload_[kept] = '\0';
    if (received != payloadTotal) LOGW(TAG, "rx: got %u of %u bytes", static_cast<unsigned>(received),
                                       static_cast<unsigned>(payloadTotal));
    if (kept < received) {
        LOGW(TAG, "rx on %s: %u bytes, larger than %u, dropped", rxTopic_, static_cast<unsigned>(received),
             static_cast<unsigned>(kMaxRxPayload));
        return;
    }
    LOGI(TAG, "rx on %s: %u bytes", rxTopic_, static_cast<unsigned>(kept));
    if (onMessage_) onMessage_(rxTopic_, rxPayload_, kept, onMessageCtx_);
}

}  // namespace services
