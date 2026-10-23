#pragma once

#include "Config.h"
#include "Fix.h"

namespace core {

// How far a fix can be trusted.
//   Good: error estimate <= goodAccuracyM, from the receiver itself. Speed and course are usable.
//   Fair: <= fairAccuracyM. Position is usable; speed and course are not.
//   Poor: anything worse, too few satellites, or no estimate at all. Not sent, not used.
enum class FixQuality : uint8_t { Good, Fair, Poor };

const char* fixQualityName(FixQuality q);

struct Accuracy {
    FixQuality quality = FixQuality::Poor;
    float errorM = 0;           // estimated horizontal error, metres (0 if unknown)
    bool fromReceiver = false;  // true: the receiver's GST estimate; false: HDOP-based guess
};

// Rates a locked reading. Prefers the receiver's own estimate (GnssReading::accuracyM, from
// NMEA GST); falls back to HDOP x hdopToMetres, capped at Fair because HDOP ignores multipath.
Accuracy assessAccuracy(const GnssReading& r, const TrackerConfig& cfg);

}  // namespace core
