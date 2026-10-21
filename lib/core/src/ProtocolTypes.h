#pragma once

#include <cstdint>
#include <optional>

// Message shapes of PROTOCOL.md v1 (../gps_tracker_backend/PROTOCOL.md).
namespace core {

constexpr int kProtocolVersion = 1;

enum class StatusEvent : uint8_t { Boot, Heartbeat, LowBattery, Shutdown };

const char* statusEventName(StatusEvent e);

struct StatusMessage {
    int64_t tsMs = 0;
    StatusEvent event = StatusEvent::Boot;
    // Optional fields: left out of the JSON when empty (the protocol forbids null).
    const char* fw = nullptr;
    const char* hw = nullptr;
    std::optional<float> batV;
    std::optional<int> rssiDbm;
    std::optional<uint32_t> uptimeS;
    // Not in protocol v1; the backend ignores unknown fields. Sent on boot to debug unexpected resets.
    const char* resetReason = nullptr;
};

}  // namespace core
