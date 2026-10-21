#pragma once

#include <Stream.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

namespace drivers {

enum class AtResult { Ok, Error, Timeout };

const char* atResultName(AtResult r);

// Receives unsolicited result codes (URCs): lines the modem sends that no command asked for,
// e.g. "+CMQTTPUB: 0,0" (PUBACK), "+CMQTTCONNLOST: 0,1", "RING".
class UrcListener {
public:
    virtual ~UrcListener() = default;
    // Return true if the line was consumed. The listener may read follow-up lines or raw bytes
    // from the channel (readLine / readRaw) before returning.
    virtual bool onUrc(const char* line) = 0;
};

// Line-oriented AT command engine over the modem UART. It is the only reader of the stream:
// every line that is not the answer to the current command is dispatched to the URC listeners
// instead of being dropped, which is what TinyGSM does and why it is not used here.
class AtChannel {
public:
    using LineFn = void (*)(const char* line, void* ctx);

    static constexpr size_t kMaxLine = 512;
    static constexpr size_t kMaxListeners = 4;

    explicit AtChannel(Stream& stream);

    void addListener(UrcListener* listener);
    void setTrace(bool on) { trace_ = on; }

    // Sends "AT<fmt...>\r" and waits for OK / ERROR / +CME ERROR.
    AtResult exec(uint32_t timeoutMs, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

    // Like exec, and copies the last line starting with `prefix` (e.g. "+CSQ:") into `out`.
    AtResult query(uint32_t timeoutMs, const char* prefix, char* out, size_t cap, const char* fmt, ...)
        __attribute__((format(printf, 6, 7)));

    // Like exec, and calls `fn` for every line starting with `prefix` (multi-line answers).
    AtResult queryLines(uint32_t timeoutMs, const char* prefix, LineFn fn, void* ctx, const char* fmt, ...)
        __attribute__((format(printf, 6, 7)));

    // Sends the command, waits for the '>' prompt, writes `len` bytes of `data`, then waits for
    // the final result. The data itself is never traced (it may be a private key).
    AtResult sendData(uint32_t timeoutMs, const void* data, size_t len, const char* fmt, ...)
        __attribute__((format(printf, 5, 6)));

    // Waits for a line starting with `prefix`, typically the asynchronous result of a command
    // that already returned OK. Other lines go to the listeners. False on timeout.
    bool waitFor(const char* prefix, char* out, size_t cap, uint32_t timeoutMs);

    // For listeners: the next non-empty line, or exactly `len` raw bytes (length-prefixed data).
    bool readLine(char* out, size_t cap, uint32_t timeoutMs);
    size_t readRaw(void* buf, size_t len, uint32_t timeoutMs);

    // Dispatches whatever complete lines are already buffered. Never blocks.
    void poll();

    // Discards everything currently in the UART buffer (used only after a modem power cycle).
    void flushInput();

private:
    enum class Read { Line, Prompt, Timeout };

    AtResult run(uint32_t timeoutMs, const char* prefix, LineFn fn, void* ctx, const void* data, size_t len,
                 const char* fmt, va_list args);
    void sendCommand(const char* fmt, va_list args);
    // Reads one line into line_. `promptOk` lets a bare '>' at line start count as a prompt.
    Read readInto(uint32_t timeoutMs, bool promptOk);
    // Non-blocking variant: consumes available bytes, true once a full line is in line_.
    bool pumpLine();
    void dispatch(const char* line);
    static bool isFinalError(const char* line);

    Stream& stream_;
    UrcListener* listeners_[kMaxListeners] = {};
    size_t listenerCount_ = 0;
    char line_[kMaxLine] = {};
    size_t len_ = 0;
    bool overflow_ = false;
    bool trace_ = false;
    // Guards against a listener calling exec() from inside a dispatch (not supported).
    bool inCommand_ = false;
};

}  // namespace drivers
