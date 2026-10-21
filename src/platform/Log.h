#pragma once

// Tagged, levelled logging to the debug UART: "[   12345] I MQTT: connected".
// Rule: never log key material or full payload bytes of certificate uploads.

#ifndef LOG_LEVEL
#define LOG_LEVEL 1  // 0 debug, 1 info, 2 warn, 3 error
#endif

namespace platform {

void logBegin(unsigned long baud);
void logPrintf(char level, const char* tag, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

}  // namespace platform

#define LOG_AT_LEVEL(lvl, ch, tag, ...)                                       \
    do {                                                                      \
        if ((lvl) >= LOG_LEVEL) ::platform::logPrintf((ch), (tag), __VA_ARGS__); \
    } while (0)

#define LOGD(tag, ...) LOG_AT_LEVEL(0, 'D', tag, __VA_ARGS__)
#define LOGI(tag, ...) LOG_AT_LEVEL(1, 'I', tag, __VA_ARGS__)
#define LOGW(tag, ...) LOG_AT_LEVEL(2, 'W', tag, __VA_ARGS__)
#define LOGE(tag, ...) LOG_AT_LEVEL(3, 'E', tag, __VA_ARGS__)
