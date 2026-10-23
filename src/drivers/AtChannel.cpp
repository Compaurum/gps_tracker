#include "AtChannel.h"

#include <Arduino.h>
#include <string.h>

#include "AtParse.h"
#include "platform/Log.h"
#include "platform/Watchdog.h"

namespace drivers {

static const char* const TAG = "AT";

// Longest command we send is CMQTTCONNECT with the endpoint host (~90 chars).
static constexpr size_t kMaxCommand = 192;
// How long to wait for a late final result after a command timed out.
static constexpr uint32_t kLateResultGraceMs = 10000;

const char* atResultName(AtResult r) {
    switch (r) {
        case AtResult::Ok: return "OK";
        case AtResult::Error: return "ERROR";
        case AtResult::Timeout: return "TIMEOUT";
    }
    return "?";
}

AtChannel::AtChannel(Stream& stream) : stream_(stream) {}

void AtChannel::addListener(UrcListener* listener) {
    if (listenerCount_ < kMaxListeners) {
        listeners_[listenerCount_++] = listener;
    } else {
        LOGE(TAG, "too many URC listeners");
    }
}

AtResult AtChannel::exec(uint32_t timeoutMs, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    AtResult r = run(timeoutMs, nullptr, nullptr, nullptr, nullptr, 0, true, fmt, args);
    va_end(args);
    return r;
}

namespace {
struct CopyTarget {
    char* out;
    size_t cap;
};
void copyLine(const char* line, void* ctx) {
    auto* t = static_cast<CopyTarget*>(ctx);
    strlcpy(t->out, line, t->cap);
}
}  // namespace

AtResult AtChannel::query(uint32_t timeoutMs, const char* prefix, char* out, size_t cap, const char* fmt, ...) {
    if (cap) out[0] = '\0';
    CopyTarget target{out, cap};
    va_list args;
    va_start(args, fmt);
    AtResult r = run(timeoutMs, prefix, copyLine, &target, nullptr, 0, true, fmt, args);
    va_end(args);
    return r;
}

AtResult AtChannel::queryLines(uint32_t timeoutMs, const char* prefix, LineFn fn, void* ctx, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    AtResult r = run(timeoutMs, prefix, fn, ctx, nullptr, 0, true, fmt, args);
    va_end(args);
    return r;
}

AtResult AtChannel::sendData(uint32_t timeoutMs, const void* data, size_t len, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    AtResult r = run(timeoutMs, nullptr, nullptr, nullptr, data, len, true, fmt, args);
    va_end(args);
    return r;
}

bool AtChannel::probe(uint32_t timeoutMs) {
    return runf(timeoutMs, false, "%s", "") == AtResult::Ok;
}

AtResult AtChannel::runf(uint32_t timeoutMs, bool waitLate, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    AtResult r = run(timeoutMs, nullptr, nullptr, nullptr, nullptr, 0, waitLate, fmt, args);
    va_end(args);
    return r;
}

AtResult AtChannel::run(uint32_t timeoutMs, const char* prefix, LineFn fn, void* ctx, const void* data, size_t len,
                        bool waitLate, const char* fmt, va_list args) {
    if (inCommand_) {
        // A URC listener tried to send a command mid-dispatch; the reply would be misattributed.
        LOGE(TAG, "nested command refused");
        return AtResult::Error;
    }
    inCommand_ = true;
    poll();

    char cmd[kMaxCommand];
    memcpy(cmd, "AT", 2);
    int n = vsnprintf(cmd + 2, sizeof(cmd) - 2, fmt, args);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(cmd) - 2) {
        LOGE(TAG, "command too long: %.40s", cmd);
        inCommand_ = false;
        return AtResult::Error;
    }
    if (trace_) LOGI(TAG, "> %s", cmd);
    stream_.write(reinterpret_cast<const uint8_t*>(cmd), strlen(cmd));
    stream_.write('\r');

    bool awaitingPrompt = data != nullptr;
    AtResult result = AtResult::Timeout;
    const uint32_t start = millis();
    for (;;) {
        const uint32_t elapsed = millis() - start;
        if (elapsed >= timeoutMs) break;
        Read r = readInto(timeoutMs - elapsed, awaitingPrompt);
        if (r == Read::Timeout) break;
        if (r == Read::Prompt) {
            if (trace_) LOGI(TAG, "> [%u bytes]", static_cast<unsigned>(len));
            stream_.write(static_cast<const uint8_t*>(data), len);
            awaitingPrompt = false;
            continue;
        }
        if (strcmp(line_, "OK") == 0) {
            // OK before the prompt would mean the data was never sent.
            result = awaitingPrompt ? AtResult::Error : AtResult::Ok;
            break;
        }
        if (isFinalError(line_)) {
            LOGW(TAG, "%s -> %s", cmd, line_);
            result = AtResult::Error;
            break;
        }
        // NMEA sentences from the GNSS stream are never the answer to a command.
        if (line_[0] == '$') {
            dispatch(line_);
            continue;
        }
        if (prefix && core::startsWith(line_, prefix)) {
            if (fn) fn(line_, ctx);
            continue;
        }
        if (strcmp(line_, cmd) == 0) continue;  // command echo, before ATE0 takes effect
        dispatch(line_);
    }
    if (result == AtResult::Timeout && waitLate) {
        LOGW(TAG, "%s -> timeout after %lu ms", cmd, static_cast<unsigned long>(timeoutMs));
        discardLateResult(cmd);
    }
    inCommand_ = false;
    return result;
}

void AtChannel::discardLateResult(const char* cmd) {
    const uint32_t start = millis();
    for (;;) {
        const uint32_t elapsed = millis() - start;
        if (elapsed >= kLateResultGraceMs) {
            LOGW(TAG, "%s: no late result within %lu ms", cmd, static_cast<unsigned long>(kLateResultGraceMs));
            return;
        }
        if (readInto(kLateResultGraceMs - elapsed, false) == Read::Timeout) continue;
        if (strcmp(line_, "OK") == 0 || isFinalError(line_)) {
            LOGW(TAG, "%s: late result '%s' discarded after %lu ms", cmd, line_,
                 static_cast<unsigned long>(millis() - start));
            return;
        }
        dispatch(line_);
    }
}

bool AtChannel::waitFor(const char* prefix, char* out, size_t cap, uint32_t timeoutMs) {
    const uint32_t start = millis();
    for (;;) {
        const uint32_t elapsed = millis() - start;
        if (elapsed >= timeoutMs) return false;
        if (readInto(timeoutMs - elapsed, false) == Read::Timeout) return false;
        if (line_[0] != '$' && core::startsWith(line_, prefix)) {
            if (out && cap) strlcpy(out, line_, cap);
            return true;
        }
        dispatch(line_);
    }
}

bool AtChannel::readLine(char* out, size_t cap, uint32_t timeoutMs) {
    const uint32_t start = millis();
    for (;;) {
        const uint32_t elapsed = millis() - start;
        if (elapsed >= timeoutMs || readInto(timeoutMs - elapsed, false) != Read::Line) return false;
        // NMEA can land between the lines of a multi-line URC; hand it on and keep reading.
        if (line_[0] == '$') {
            dispatch(line_);
            continue;
        }
        strlcpy(out, line_, cap);
        return true;
    }
}

size_t AtChannel::readRaw(void* buf, size_t len, uint32_t timeoutMs) {
    auto* dst = static_cast<uint8_t*>(buf);
    size_t got = 0;
    // Bytes already pulled into a partial line belong to the raw data.
    while (len_ > 0 && got < len) {
        dst[got++] = static_cast<uint8_t>(line_[0]);
        memmove(line_, line_ + 1, --len_);
    }
    const uint32_t start = millis();
    while (got < len && millis() - start < timeoutMs) {
        int c = stream_.read();
        if (c < 0) {
            platform::watchdog::feed();
            delay(1);
            continue;
        }
        dst[got++] = static_cast<uint8_t>(c);
    }
    if (trace_) LOGI(TAG, "< [%u raw bytes]", static_cast<unsigned>(got));
    return got;
}

void AtChannel::poll() {
    while (pumpLine()) dispatch(line_);
}

void AtChannel::flushInput() {
    while (stream_.read() >= 0) {
    }
    len_ = 0;
    overflow_ = false;
}

AtChannel::Read AtChannel::readInto(uint32_t timeoutMs, bool promptOk) {
    const uint32_t start = millis();
    for (;;) {
        int c = stream_.read();
        if (c < 0) {
            if (millis() - start >= timeoutMs) return Read::Timeout;
            platform::watchdog::feed();
            delay(1);
            continue;
        }
        if (c == '\r') continue;
        if (c == '\n') {
            while (len_ > 0 && line_[len_ - 1] == ' ') --len_;
            if (len_ == 0) continue;  // blank line between responses
            line_[len_] = '\0';
            len_ = 0;
            if (overflow_) LOGW(TAG, "line truncated to %u bytes", static_cast<unsigned>(kMaxLine - 1));
            overflow_ = false;
            // The GNSS NMEA stream (3 lines a second) would drown the trace.
            if (trace_ && line_[0] != '$') LOGI(TAG, "< %s", line_);
            return Read::Line;
        }
        if (len_ == 0 && c == ' ') continue;  // the space after a '>' prompt
        if (promptOk && len_ == 0 && c == '>') return Read::Prompt;
        if (len_ < kMaxLine - 1) {
            line_[len_++] = static_cast<char>(c);
        } else {
            overflow_ = true;
        }
    }
}

bool AtChannel::pumpLine() {
    while (stream_.available() > 0) {
        if (readInto(0, false) == Read::Line) return true;
    }
    return false;
}

void AtChannel::dispatch(const char* line) {
    // Listeners may read further lines, which overwrites line_; give them a stable copy.
    char copy[kMaxLine];
    strlcpy(copy, line, sizeof(copy));
    for (size_t i = 0; i < listenerCount_; ++i) {
        if (listeners_[i]->onUrc(copy)) return;
    }
    LOGD(TAG, "unhandled: %s", copy);
}

bool AtChannel::isFinalError(const char* line) {
    return strcmp(line, "ERROR") == 0 || core::startsWith(line, "+CME ERROR") || core::startsWith(line, "+CMS ERROR");
}

}  // namespace drivers
