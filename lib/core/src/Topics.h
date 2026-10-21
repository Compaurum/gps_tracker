#pragma once

#include <cstddef>

// MQTT topics of PROTOCOL.md v1. The device may touch only its own three; anything else makes
// IoT Core drop the connection, so topics are only ever built here.
namespace core {

enum class Topic { Telemetry, Status, Cmd };

// Device IDs are [a-zA-Z0-9_-]{1,64}, identical to the MQTT client ID and IoT thing name.
bool isValidDeviceId(const char* id);

// Writes "trackers/<id>/<kind>". Returns false if the ID is invalid or `cap` is too small.
bool formatTopic(char* out, size_t cap, const char* deviceId, Topic topic);

}  // namespace core
