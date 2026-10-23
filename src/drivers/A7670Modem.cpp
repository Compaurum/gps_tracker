#include "A7670Modem.h"

#include <Arduino.h>
#include <string.h>

#include "AtParse.h"
#include "platform/Board.h"
#include "platform/Log.h"
#include "platform/Watchdog.h"

namespace drivers {

static const char* const TAG = "MODEM";

A7670Modem::A7670Modem(AtChannel& at) : at_(at) {
    at_.addListener(this);
}

bool A7670Modem::probe(uint32_t attempts) {
    for (uint32_t i = 0; i < attempts; ++i) {
        if (at_.probe(500)) return true;
    }
    return false;
}

bool A7670Modem::powerOn() {
    const uint32_t start = millis();
    if (probe(3)) {
        LOGI(TAG, "already on");
    } else {
        LOGI(TAG, "power-on sequence");
        platform::board::modemPowerOnSequence();
        bool up = false;
        // LilyGO's examples re-pulse PWRKEY when the modem stays silent for a while.
        for (int round = 0; round < 3 && !up; ++round) {
            const uint32_t roundStart = millis();
            while (millis() - roundStart < 15000) {
                if (at_.probe(500)) {
                    up = true;
                    break;
                }
                platform::watchdog::feed();
            }
            if (!up) {
                LOGW(TAG, "no answer, pulsing PWRKEY again");
                platform::board::modemPowerKeyPulse();
            }
        }
        if (!up) {
            LOGE(TAG, "modem does not answer AT");
            return false;
        }
    }
    // Echo off, verbose errors.
    at_.exec(1000, "E0");
    at_.exec(1000, "+CMEE=2");
    LOGI(TAG, "answers AT after %lu ms", static_cast<unsigned long>(millis() - start));
    return true;
}

void A7670Modem::reset() {
    if (at_.exec(3000, "+CRESET") != AtResult::Ok) {
        LOGW(TAG, "CRESET refused, using the reset pin");
        platform::board::modemPowerOnSequence();
    }
    // The modem needs several seconds before it answers again; powerOn() polls for it.
    delay(5000);
    at_.flushInput();
}

bool A7670Modem::isSimReady() {
    char line[64];
    if (at_.query(2000, "+CPIN:", line, sizeof(line), "+CPIN?") != AtResult::Ok) return false;
    if (strstr(line, "READY")) return true;
    LOGW(TAG, "SIM: %s", line);
    return false;
}

bool A7670Modem::isRegistered(int* stat, const char** rat) {
    char line[64];
    int s = -1;
    if (at_.query(2000, "+CEREG:", line, sizeof(line), "+CEREG?") == AtResult::Ok &&
        core::parseRegistered(line, "+CEREG:", &s)) {
        if (stat) *stat = s;
        if (rat) *rat = "LTE";
        return true;
    }
    if (stat) *stat = s;  // LTE status: 0 not searching, 2 searching, 3 denied, 4 unknown
    int s2g = -1;
    if (at_.query(2000, "+CREG:", line, sizeof(line), "+CREG?") == AtResult::Ok &&
        core::parseRegistered(line, "+CREG:", &s2g)) {
        if (stat) *stat = s2g;
        if (rat) *rat = "2G";
        return true;
    }
    return false;
}

bool A7670Modem::activateData(const char* apn) {
    if (apn && apn[0]) {
        if (at_.exec(2000, "+CGDCONT=1,\"IP\",\"%s\"", apn) != AtResult::Ok) return false;
    }
    char line[64];
    if (at_.query(5000, "+CGACT:", line, sizeof(line), "+CGACT?") == AtResult::Ok) {
        int v[2];
        if (core::parseInts(line, "+CGACT:", v, 2) == 2 && v[0] == 1 && v[1] == 1) return true;
    }
    LOGI(TAG, "activating PDP context");
    return at_.exec(30000, "+CGACT=1,1") == AtResult::Ok;
}

std::optional<int> A7670Modem::rssiDbm() {
    char line[32];
    if (at_.query(2000, "+CSQ:", line, sizeof(line), "+CSQ") != AtResult::Ok) return std::nullopt;
    return core::parseCsqDbm(line);
}

std::optional<int64_t> A7670Modem::ntpTimeMs(const char* server) {
    // Timezone 0, so the modem RTC (and +CCLK) reads UTC.
    if (at_.exec(2000, "+CNTP=\"%s\",0", server) != AtResult::Ok) return std::nullopt;
    if (at_.exec(2000, "+CNTP") != AtResult::Ok) return std::nullopt;
    char line[48];
    if (!at_.waitFor("+CNTP:", line, sizeof(line), 30000)) {
        LOGW(TAG, "NTP: no result");
        return std::nullopt;
    }
    int code = -1;
    core::parseInts(line, "+CNTP:", &code, 1);
    if (code != 0) {
        LOGW(TAG, "NTP failed: %s", line);
        return std::nullopt;
    }
    // CNTP only sets the RTC; read it back (whole seconds).
    if (at_.query(2000, "+CCLK:", line, sizeof(line), "+CCLK?") != AtResult::Ok) return std::nullopt;
    return core::parseCclk(line);
}

namespace {
struct CertSearch {
    const char* const* names;
    bool* present;
    size_t n;
};
void matchCert(const char* line, void* ctx) {
    auto* s = static_cast<CertSearch*>(ctx);
    // +CCERTLIST: "device_cert.pem"
    const char* q = strchr(line, '"');
    if (!q) return;
    for (size_t i = 0; i < s->n; ++i) {
        const size_t len = strlen(s->names[i]);
        if (strncmp(q + 1, s->names[i], len) == 0 && q[1 + len] == '"') s->present[i] = true;
    }
}
}  // namespace

bool A7670Modem::findCertificates(const char* const* names, bool* present, size_t n) {
    for (size_t i = 0; i < n; ++i) present[i] = false;
    CertSearch s{names, present, n};
    return at_.queryLines(5000, "+CCERTLIST:", matchCert, &s, "+CCERTLIST") == AtResult::Ok;
}

bool A7670Modem::hasCertificate(const char* name) {
    bool present = false;
    return findCertificates(&name, &present, 1) && present;
}

bool A7670Modem::uploadCertificate(const char* name, const char* pem) {
    const size_t len = strlen(pem);
    // Replace, don't append: delete first and ignore "not found".
    at_.exec(5000, "+CCERTDELE=\"%s\"", name);
    AtResult r = at_.sendData(10000, pem, len, "+CCERTDOWN=\"%s\",%u", name, static_cast<unsigned>(len));
    if (r != AtResult::Ok) {
        LOGE(TAG, "upload of %s failed: %s", name, atResultName(r));
        return false;
    }
    LOGI(TAG, "uploaded %s (%u bytes)", name, static_cast<unsigned>(len));
    return true;
}

void A7670Modem::logIdentity() {
    char line[96];
    if (at_.query(2000, "", line, sizeof(line), "+CGMM") == AtResult::Ok) LOGI(TAG, "model: %s", line);
    if (at_.query(2000, "+CGMR:", line, sizeof(line), "+CGMR") == AtResult::Ok) LOGI(TAG, "firmware: %s", line);
}

bool A7670Modem::onUrc(const char* line) {
    // Boot and status chatter the modem sends on its own; nothing to act on yet.
    static const char* const kNoise[] = {"RDY", "*ATREADY:", "+CPIN:", "SMS DONE", "PB DONE",
                                         "+CGEV:", "+CTZV:", "*PSUTTZ", "DST:", "+CREG:", "+CEREG:",
                                         "+MSTK:"};  // SIM Toolkit chatter from the SIM card
    for (const char* n : kNoise) {
        if (core::startsWith(line, n)) {
            LOGD(TAG, "urc: %s", line);
            return true;
        }
    }
    return false;
}

}  // namespace drivers
