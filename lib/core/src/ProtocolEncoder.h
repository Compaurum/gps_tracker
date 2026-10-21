#pragma once

#include <cstddef>

#include "ProtocolTypes.h"

namespace core {

// Encodes `msg` as the JSON payload for trackers/<id>/status.
// Returns the payload length, or 0 if it didn't fit in `cap` or the message is invalid
// (implausible timestamp, NaN battery voltage). Never returns a truncated payload.
size_t encodeStatus(const StatusMessage& msg, char* buf, size_t cap);

}  // namespace core
