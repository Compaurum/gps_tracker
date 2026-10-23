#pragma once

#include <cstddef>

#include "Fix.h"
#include "ProtocolTypes.h"

namespace core {

// Encodes `msg` as the JSON payload for trackers/<id>/status.
// Returns the payload length, or 0 if it didn't fit in `cap` or the message is invalid
// (implausible timestamp, NaN battery voltage). Never returns a truncated payload.
size_t encodeStatus(const StatusMessage& msg, char* buf, size_t cap);

// Encodes `msg` with `n` fixes (oldest first) as the payload for trackers/<id>/telemetry.
// Returns 0 if n is outside 1..100, the envelope ts is implausible, or the result doesn't fit.
size_t encodeTelemetry(const TelemetryMessage& msg, const Fix* fixes, size_t n, char* buf, size_t cap);

}  // namespace core
