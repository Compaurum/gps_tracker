#include "Topics.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace core {

bool isValidDeviceId(const char* id) {
    if (!id) return false;
    size_t n = strlen(id);
    if (n < 1 || n > 64) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = static_cast<unsigned char>(id[i]);
        if (!isalnum(c) && c != '_' && c != '-') return false;
    }
    return true;
}

static const char* topicSuffix(Topic t) {
    switch (t) {
        case Topic::Telemetry: return "telemetry";
        case Topic::Status: return "status";
        case Topic::Cmd: return "cmd";
    }
    return "status";
}

bool formatTopic(char* out, size_t cap, const char* deviceId, Topic topic) {
    if (!out || cap == 0 || !isValidDeviceId(deviceId)) return false;
    int n = snprintf(out, cap, "trackers/%s/%s", deviceId, topicSuffix(topic));
    return n > 0 && static_cast<size_t>(n) < cap;
}

}  // namespace core
