#include "Log.h"

#include <Arduino.h>
#include <stdarg.h>

namespace platform {

void logBegin(unsigned long baud) {
    Serial.begin(baud);
}

void logPrintf(char level, const char* tag, const char* fmt, ...) {
    char msg[256];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    // Mark truncation instead of silently cutting a line short.
    const char* more = (n >= static_cast<int>(sizeof(msg))) ? "..." : "";
    Serial.printf("[%8lu] %c %s: %s%s\n", millis(), level, tag, msg, more);
}

}  // namespace platform
