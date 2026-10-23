#pragma once

#include <stdint.h>

#include "Tracking.h"
#include "drivers/A7670Modem.h"
#include "services/ClockService.h"
#include "services/MqttSession.h"
#include "services/NetworkLink.h"
#include "services/Uplink.h"

namespace app {

// The link state machine (docs/ARCHITECTURE.md §4, P1-P2 subset): bring the link up, send
// `boot` once per power-up, stay connected, recover with backoff. Tracking (sampling, outbox,
// reporting) runs alongside: sampling whenever the modem is up, reporting only while ONLINE.
// The sleep states arrive with LifecyclePolicy in P3.
class Lifecycle {
public:
    struct Deps {
        drivers::A7670Modem& modem;
        services::NetworkLink& net;
        services::ClockService& clock;
        services::MqttSession& mqtt;
        services::Uplink& uplink;
        Tracking& tracking;
    };

    explicit Lifecycle(const Deps& deps);

    void begin();
    void tick();

private:
    enum class State { PowerOn, Network, Clock, Mqtt, Online, Backoff, Halted };

    static const char* stateName(State s);
    void transition(State next, const char* reason);
    void fail(const char* reason);
    bool configLooksValid();
    bool certificatesPresent();
    bool sendBoot();

    void stepPowerOn();
    void stepNetwork();
    void stepClock();
    void stepMqtt();
    void stepOnline();
    void stepBackoff();
    void stepHalted();

    Deps d_;
    State state_ = State::PowerOn;
    State resumeState_ = State::Network;
    uint32_t enteredAtMs_ = 0;
    uint32_t backoffMs_ = 0;
    uint32_t consecutiveFailures_ = 0;
    uint32_t lastHaltLogMs_ = 0;
    bool bootSent_ = false;
    bool certsChecked_ = false;
    // The modem answers AT; GNSS sampling may run.
    bool modemReady_ = false;
};

}  // namespace app
