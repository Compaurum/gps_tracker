#pragma once

#include <stdint.h>

// Build-time device configuration. DEVICE_ID, MQTT_HOST and CELL_APN come from secrets.ini
// through platformio.ini build flags; FW_VERSION from the [firmware] section.
namespace config {

constexpr const char* kDeviceId = DEVICE_ID;
constexpr const char* kMqttHost = MQTT_HOST;
constexpr uint16_t kMqttPort = 8883;
constexpr const char* kApn = CELL_APN;

constexpr const char* kFwVersion = FW_VERSION;
// Lowercase, dash-separated, stable per device type (PROTOCOL.md, status.hw).
constexpr const char* kHwModel = "lilygo-t-call-a7670";

constexpr uint16_t kMqttKeepaliveS = 60;
constexpr const char* kNtpServer = "pool.ntp.org";

// File names in the modem's certificate store, written once by the provision env.
constexpr const char* kCaCertFile = "aws_root_ca.pem";
constexpr const char* kClientCertFile = "device_cert.pem";
constexpr const char* kClientKeyFile = "device_key.pem";

}  // namespace config
