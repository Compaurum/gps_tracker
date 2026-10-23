#include "ModemGnss.h"

#include <Arduino.h>
#include <string.h>

#include "AtParse.h"
#include "GnssParse.h"
#include "platform/Log.h"

namespace drivers {

static const char* const TAG = "GNSS";

// The receiver reports "+CGNSSPWR: READY!" once it has loaded its firmware.
static constexpr uint32_t kPowerOnTimeoutMs = 30000;
// NMEA comes once a second; an RMC older than this means the stream stopped.
static constexpr uint32_t kNmeaMaxAgeMs = 3000;
// No NMEA at all this long after enabling it: the modem doesn't stream, poll CGNSSINFO instead.
static constexpr uint32_t kNmeaStartGraceMs = 15000;
// Assistance download from SIMCom's server over the data link.
static constexpr uint32_t kAssistTimeoutMs = 40000;

ModemGnss::ModemGnss(AtChannel& at) : at_(at) {
    at_.addListener(this);
}

bool ModemGnss::powerOn() {
    char line[48];
    bool running = false;
    if (at_.query(2000, "+CGNSSPWR:", line, sizeof(line), "+CGNSSPWR?") == AtResult::Ok) {
        int v[1] = {0};
        running = core::parseInts(line, "+CGNSSPWR:", v, 1) == 1 && v[0] == 1;
    }
    if (running) {
        LOGI(TAG, "already on");
    } else {
        if (at_.exec(5000, "+CGNSSPWR=1") != AtResult::Ok) return false;
        if (!at_.waitFor("+CGNSSPWR: READY", line, sizeof(line), kPowerOnTimeoutMs)) {
            LOGW(TAG, "no READY after %lu ms", static_cast<unsigned long>(kPowerOnTimeoutMs));
            return false;
        }
    }
    // Constellation choice; a refusal leaves the modem's default, which still works.
    if (at_.exec(2000, "+CGNSSMODE=%d", kGnssMode) != AtResult::Ok) LOGW(TAG, "CGNSSMODE=%d refused", kGnssMode);
    char mode[32];
    if (at_.query(2000, "+CGNSSMODE:", mode, sizeof(mode), "+CGNSSMODE?") == AtResult::Ok) LOGI(TAG, "%s", mode);
    configureOutput();
    on_ = true;
    return true;
}

void ModemGnss::configureOutput() {
    // Sentence selection: GGA, GLL, GSA, GSV, RMC, VTG, ZDA, GST. Only what we use, to keep
    // the shared UART quiet (~250 bytes per second).
    if (at_.exec(2000, "+CGNSSNMEA=1,0,0,0,1,0,0,1") != AtResult::Ok) {
        LOGW(TAG, "CGNSSNMEA refused; the modem's default sentence set stays on");
    }
    // NMEA to the AT port, as LilyGO's TinyGSM fork does for the A76xx.
    if (at_.exec(2000, "+CGNSSTST=1") != AtResult::Ok) LOGW(TAG, "CGNSSTST=1 refused");
    if (at_.exec(2000, "+CGNSSPORTSWITCH=0,1") != AtResult::Ok) LOGW(TAG, "CGNSSPORTSWITCH refused");
    configuredAtMs_ = millis();
    fallbackLogged_ = false;
}

void ModemGnss::powerOff() {
    at_.exec(2000, "+CGNSSTST=0");
    at_.exec(5000, "+CGNSSPWR=0");
    on_ = false;
}

std::optional<core::GnssReading> ModemGnss::read() {
    const uint32_t now = millis();
    if (nmea_.anySentence()) {
        if (nmea_.gstSeen() && !gstLogged_) {
            LOGI(TAG, "receiver reports its own error estimate (GST)");
            gstLogged_ = true;
        }
        // nullopt once the stream stops; PositionService re-powers the receiver if that persists.
        return nmea_.latest(now, kNmeaMaxAgeMs);
    }
    if (now - configuredAtMs_ < kNmeaStartGraceMs) return std::nullopt;
    if (!fallbackLogged_) {
        LOGW(TAG, "no NMEA output; polling AT+CGNSSINFO instead (no error estimate)");
        fallbackLogged_ = true;
    }
    return readCgnssInfo();
}

std::optional<core::GnssReading> ModemGnss::readCgnssInfo() {
    char line[200];
    if (at_.query(3000, "+CGNSSINFO:", line, sizeof(line), "+CGNSSINFO") != AtResult::Ok) return std::nullopt;
    auto r = core::parseCgnssInfo(line);
    if (!r) LOGW(TAG, "unparsed: %s", line);
    return r;
}

bool ModemGnss::assist() {
    // "+AGPS: success." follows (or precedes) the OK once the download is done.
    char line[48];
    const uint32_t start = millis();
    if (at_.query(kAssistTimeoutMs, "+AGPS:", line, sizeof(line), "+CAGPS") != AtResult::Ok) {
        LOGW(TAG, "assisted GPS refused");
        return false;
    }
    if (!line[0] && !at_.waitFor("+AGPS:", line, sizeof(line), kAssistTimeoutMs)) {
        LOGW(TAG, "assisted GPS: no result");
        return false;
    }
    const bool ok = strstr(line, "success") != nullptr;
    if (ok) {
        LOGI(TAG, "assisted GPS data loaded in %lu ms", static_cast<unsigned long>(millis() - start));
    } else {
        LOGW(TAG, "assisted GPS failed: %s", line);
    }
    return ok;
}

bool ModemGnss::onUrc(const char* line) {
    if (line[0] == '$') {
        nmea_.feed(line, millis());
        return true;
    }
    // Late power-on notices, e.g. after a modem reset switched the receiver back on.
    if (core::startsWith(line, "+CGNSSPWR:")) {
        LOGD(TAG, "urc: %s", line);
        return true;
    }
    if (core::startsWith(line, "+AGPS:")) {
        LOGI(TAG, "late assisted GPS result: %s", line);
        return true;
    }
    return false;
}

}  // namespace drivers
