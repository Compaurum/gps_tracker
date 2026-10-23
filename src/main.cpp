// Wiring only: build the concrete modules for this board and run the lifecycle.
// Architecture: docs/ARCHITECTURE.md
#include <Arduino.h>
#include <esp32-hal-psram.h>

#include "Config.h"
#include "FixOutbox.h"
#include "app/Lifecycle.h"
#include "app/Tracking.h"
#include "config/DeviceConfig.h"
#include "drivers/A7670Modem.h"
#include "drivers/AtChannel.h"
#include "drivers/gnss/ModemGnss.h"
#include "platform/Board.h"
#include "platform/Log.h"
#include "platform/Watchdog.h"
#include "services/ClockService.h"
#include "services/MqttSession.h"
#include "services/NetworkLink.h"
#include "services/PositionService.h"
#include "services/Uplink.h"

#ifndef AT_TRACE
#define AT_TRACE 0
#endif

static app::Lifecycle* lifecycle = nullptr;

// Store-and-forward capacity. 24 h of riding at one fix per 10 s, in PSRAM (24 B each, ~200 KB);
// RAM is lost on power-off either way until P3 adds persistence.
static constexpr size_t kOutboxFixesPsram = 8640;
static constexpr size_t kOutboxFixesNoPsram = 1000;

static core::FixOutbox makeOutbox() {
    size_t n = psramFound() ? kOutboxFixesPsram : kOutboxFixesNoPsram;
    void* mem = psramFound() ? ps_malloc(n * sizeof(core::Fix)) : malloc(n * sizeof(core::Fix));
    if (!mem) {
        LOGE("MAIN", "no memory for the fix outbox");
        n = 0;
    } else {
        LOGI("MAIN", "fix outbox: %u fixes in %s", static_cast<unsigned>(n), psramFound() ? "PSRAM" : "RAM");
    }
    return core::FixOutbox(static_cast<core::Fix*>(mem), n);
}

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

    static const core::TrackerConfig trackerCfg;
    static drivers::ModemGnss gnss(at);
    static services::PositionService position(gnss, clock, trackerCfg);
    static core::FixOutbox outbox = makeOutbox();
    static app::Tracking tracking({position, uplink, modem, outbox, trackerCfg});

    static app::Lifecycle life({modem, net, clock, mqtt, uplink, tracking});
    lifecycle = &life;
    lifecycle->begin();
}

void loop() {
    platform::watchdog::feed();
    lifecycle->tick();
    delay(10);
}
