#include "ProtocolEncoder.h"

#include <ArduinoJson.h>

#include <cmath>

#include "ClockMath.h"

namespace core {

const char* statusEventName(StatusEvent e) {
    switch (e) {
        case StatusEvent::Boot: return "boot";
        case StatusEvent::Heartbeat: return "heartbeat";
        case StatusEvent::LowBattery: return "low_battery";
        case StatusEvent::Shutdown: return "shutdown";
    }
    return "boot";
}

// Rounds to `decimals` places so the wire shows e.g. 4.12, not 4.119999885.
static double roundTo(double v, int decimals) {
    const double scale = std::pow(10.0, decimals);
    return std::round(v * scale) / scale;
}

// Serializes into buf only if the whole document fits; never leaves a truncated payload.
static size_t serializeWhole(const JsonDocument& doc, char* buf, size_t cap) {
    if (doc.overflowed()) return 0;
    const size_t need = measureJson(doc);
    if (need + 1 > cap) return 0;
    return serializeJson(doc, buf, cap);
}

size_t encodeStatus(const StatusMessage& msg, char* buf, size_t cap) {
    // The backend drops a message with an implausible ts, and never says so.
    if (msg.tsMs < kMinPlausibleEpochMs) return 0;
    // JSON has no NaN/Infinity; refusing is safer than sending something the backend drops.
    if (msg.batV && !std::isfinite(*msg.batV)) return 0;

    JsonDocument doc;
    doc["v"] = kProtocolVersion;
    doc["ts"] = msg.tsMs;
    doc["event"] = statusEventName(msg.event);
    if (msg.fw) doc["fw"] = msg.fw;
    if (msg.hw) doc["hw"] = msg.hw;
    if (msg.batV) doc["batV"] = roundTo(*msg.batV, 2);
    if (msg.rssiDbm) doc["rssi"] = *msg.rssiDbm;
    if (msg.uptimeS) doc["uptime"] = *msg.uptimeS;
    if (msg.resetReason) doc["rst"] = msg.resetReason;
    return serializeWhole(doc, buf, cap);
}

}  // namespace core
