#include "Lifecycle.h"

#include <Arduino.h>
#include <string.h>

#include "Endpoint.h"
#include "config/DeviceConfig.h"
#include "platform/Board.h"
#include "platform/Log.h"

namespace app {

static const char* const TAG = "LIFE";

static constexpr uint32_t kBackoffFirstMs = 5000;
static constexpr uint32_t kBackoffMaxMs = 300000;
// After this many failures in a row, reset the modem itself rather than retrying on top of
// whatever state it is stuck in.
static constexpr uint32_t kFailuresBeforeModemReset = 5;

Lifecycle::Lifecycle(const Deps& deps) : d_(deps) {}

const char* Lifecycle::stateName(State s) {
    switch (s) {
        case State::PowerOn: return "POWER_ON";
        case State::Network: return "NETWORK";
        case State::Clock: return "CLOCK";
        case State::Mqtt: return "MQTT";
        case State::Online: return "ONLINE";
        case State::Backoff: return "BACKOFF";
        case State::Halted: return "HALTED";
    }
    return "?";
}

void Lifecycle::begin() {
    LOGI(TAG, "fw %s, hw %s, device %s, reset reason %s", config::kFwVersion, config::kHwModel, config::kDeviceId,
         platform::board::resetReason());
    enteredAtMs_ = millis();
    if (!configLooksValid() || !d_.uplink.begin()) {
        transition(State::Halted, "invalid configuration");
        return;
    }
    LOGI(TAG, "start in %s", stateName(state_));
}

void Lifecycle::tick() {
    if (modemReady_) {
        // Drain URCs (NMEA, MQTT notices) every pass, whatever the state.
        d_.modem.at().poll();
        d_.tracking.sample(millis());
    }
    switch (state_) {
        case State::PowerOn: stepPowerOn(); break;
        case State::Network: stepNetwork(); break;
        case State::Clock: stepClock(); break;
        case State::Mqtt: stepMqtt(); break;
        case State::Online: stepOnline(); break;
        case State::Backoff: stepBackoff(); break;
        case State::Halted: stepHalted(); break;
    }
}

void Lifecycle::transition(State next, const char* reason) {
    LOGI(TAG, "%s -> %s (%s, after %lu ms)", stateName(state_), stateName(next), reason,
         static_cast<unsigned long>(millis() - enteredAtMs_));
    state_ = next;
    enteredAtMs_ = millis();
    if (next == State::Network) d_.net.begin(enteredAtMs_);
    platform::board::setLed(next == State::Online);
}

void Lifecycle::fail(const char* reason) {
    ++consecutiveFailures_;
    backoffMs_ = backoffMs_ == 0 ? kBackoffFirstMs : backoffMs_ * 2;
    if (backoffMs_ > kBackoffMaxMs) backoffMs_ = kBackoffMaxMs;
    if (consecutiveFailures_ >= kFailuresBeforeModemReset) {
        LOGW(TAG, "%lu failures in a row, resetting modem", static_cast<unsigned long>(consecutiveFailures_));
        d_.modem.reset();
        modemReady_ = false;
        d_.tracking.onModemReset();
        consecutiveFailures_ = 0;
        resumeState_ = State::PowerOn;
    } else {
        // Retry the failed step itself where that makes sense; otherwise re-check the network.
        resumeState_ = (state_ == State::PowerOn || state_ == State::Clock) ? state_ : State::Network;
    }
    LOGW(TAG, "%s failed: %s; retry in %lu s", stateName(state_), reason, static_cast<unsigned long>(backoffMs_ / 1000));
    transition(State::Backoff, reason);
}

bool Lifecycle::configLooksValid() {
    if (strstr(config::kMqttHost, "CHANGE-ME") || strlen(config::kMqttHost) == 0) {
        LOGE(TAG, "mqtt_host is not set in secrets.ini");
        return false;
    }
    if (!core::isValidHostName(config::kMqttHost)) {
        LOGE(TAG, "mqtt_host '%s' must be a bare host name: no port (':8883'), scheme or path", config::kMqttHost);
        return false;
    }
    return true;
}

bool Lifecycle::certificatesPresent() {
    const char* const names[] = {config::kCaCertFile, config::kClientCertFile, config::kClientKeyFile};
    bool present[3] = {};
    if (!d_.modem.findCertificates(names, present, 3)) {
        LOGE(TAG, "could not list the modem's certificates");
        return false;
    }
    bool ok = true;
    for (size_t i = 0; i < 3; ++i) {
        if (!present[i]) {
            LOGE(TAG, "certificate %s missing from modem; flash the provision env first (README)", names[i]);
            ok = false;
        }
    }
    return ok;
}

bool Lifecycle::sendBoot() {
    core::StatusMessage msg;
    msg.event = core::StatusEvent::Boot;
    msg.fw = config::kFwVersion;
    msg.hw = config::kHwModel;
    msg.rssiDbm = d_.modem.rssiDbm();
    msg.uptimeS = millis() / 1000;
    msg.resetReason = platform::board::resetReason();
    return d_.uplink.sendStatus(msg) == services::PublishResult::Delivered;
}

void Lifecycle::stepPowerOn() {
    if (!d_.modem.powerOn()) return fail("modem not answering");
    modemReady_ = true;
    d_.modem.logIdentity();
    if (!certsChecked_) {
        if (!certificatesPresent()) return transition(State::Halted, "certificates missing");
        certsChecked_ = true;
    }
    transition(State::Network, "modem ready");
}

void Lifecycle::stepNetwork() {
    switch (d_.net.poll(millis())) {
        case services::NetworkLink::Status::Pending: return;
        case services::NetworkLink::Status::Failed: return fail("network");
        case services::NetworkLink::Status::Up: break;
    }
    // GNSS may have set the clock already; NTP is only needed without a lock.
    transition(d_.clock.isSet() ? State::Mqtt : State::Clock, "registered");
}

void Lifecycle::stepClock() {
    // GNSS may have set the clock while we were backing off.
    if (d_.clock.isSet()) return transition(State::Mqtt, "clock set by gnss");
    if (!d_.clock.syncFromNtp(config::kNtpServers, config::kNtpServerCount)) return fail("clock");
    transition(State::Mqtt, "clock set");
}

void Lifecycle::stepMqtt() {
    if (!d_.mqtt.connect()) return fail("mqtt connect");
    // The cmd topic is reserved: subscribe now, as the protocol asks. A refusal isn't fatal.
    if (!d_.mqtt.subscribe(d_.uplink.cmdTopic())) LOGW(TAG, "cmd subscribe failed, continuing");
    // boot goes out once per power-up; a reconnect resumes without it.
    const char* why = bootSent_ ? "reconnected" : "boot delivered";
    if (!bootSent_) {
        if (!sendBoot()) return fail("boot not delivered");
        bootSent_ = true;
    }
    consecutiveFailures_ = 0;
    backoffMs_ = 0;
    transition(State::Online, why);
    d_.tracking.onLinkUp();
}

void Lifecycle::stepOnline() {
    if (!d_.mqtt.isConnected()) return fail("mqtt connection lost");
    d_.tracking.report(millis());
}

void Lifecycle::stepBackoff() {
    if (millis() - enteredAtMs_ >= backoffMs_) transition(resumeState_, "backoff over");
}

void Lifecycle::stepHalted() {
    // Nothing will fix itself; keep saying why so a fresh monitor session sees it.
    if (millis() - lastHaltLogMs_ >= 30000) {
        LOGE(TAG, "halted: fix the configuration above and reflash");
        lastHaltLogMs_ = millis();
    }
}

}  // namespace app
