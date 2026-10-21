// Wiring only: build the concrete modules for this board and run the lifecycle.
// Architecture: docs/ARCHITECTURE.md
#include <Arduino.h>

#include "app/Lifecycle.h"
#include "config/DeviceConfig.h"
#include "drivers/A7670Modem.h"
#include "drivers/AtChannel.h"
#include "platform/Board.h"
#include "platform/Log.h"
#include "platform/Watchdog.h"
#include "services/ClockService.h"
#include "services/MqttSession.h"
#include "services/NetworkLink.h"
#include "services/Uplink.h"

#ifndef AT_TRACE
#define AT_TRACE 0
#endif

static app::Lifecycle* lifecycle = nullptr;

static void onCommand(const char* topic, const uint8_t* payload, size_t len, void*) {
    // CommandService arrives with remote wake (P4). The protocol says to ignore unknown commands.
    LOGI("CMD", "ignored %u bytes on %s: %.*s", static_cast<unsigned>(len), topic, static_cast<int>(len),
         reinterpret_cast<const char*>(payload));
}

void setup() {
    platform::logBegin(115200);
    platform::board::initPins();
    // Longest single wait is a PUBACK (~70 s), but every wait loop feeds the watchdog.
    platform::watchdog::begin(30);

    // Objects are built here, not at namespace scope, so the UART opens after the Arduino core is up.
    static drivers::AtChannel at(platform::board::modemSerial());
    at.setTrace(AT_TRACE);
    static drivers::A7670Modem modem(at);
    static services::NetworkLink net(modem, config::kApn);
    static services::ClockService clock(modem);
    static services::MqttSession mqtt(at, services::MqttConfig{
                                              config::kMqttHost,
                                              config::kMqttPort,
                                              config::kDeviceId,
                                              config::kCaCertFile,
                                              config::kClientCertFile,
                                              config::kClientKeyFile,
                                              config::kMqttKeepaliveS,
                                          });
    mqtt.setMessageHandler(onCommand, nullptr);
    static services::Uplink uplink(mqtt, clock, config::kDeviceId);
    static app::Lifecycle life({modem, net, clock, mqtt, uplink});
    lifecycle = &life;
    lifecycle->begin();
}

void loop() {
    platform::watchdog::feed();
    lifecycle->tick();
    delay(10);
}
