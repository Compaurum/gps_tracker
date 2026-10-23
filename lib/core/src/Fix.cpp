#include "Fix.h"

#include <cmath>

#include "ClockMath.h"

namespace core {

template <typename T>
static T clampRound(double v, double lo, double hi) {
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return static_cast<T>(std::lround(v));
}

std::optional<Fix> fixFromReading(const GnssReading& r) {
    if (!r.hasFix) return std::nullopt;
    if (!std::isfinite(r.latDeg) || !std::isfinite(r.lonDeg)) return std::nullopt;
    if (r.latDeg < -90 || r.latDeg > 90 || r.lonDeg < -180 || r.lonDeg > 180) return std::nullopt;
    // Receivers report exactly 0,0 before they have a position; the backend drops it too.
    if (r.latDeg == 0 && r.lonDeg == 0) return std::nullopt;
    if (r.timeMs < kMinPlausibleEpochMs) return std::nullopt;

    Fix f;
    f.tsS = static_cast<uint32_t>(r.timeMs / 1000);
    f.latE7 = static_cast<int32_t>(std::llround(r.latDeg * 1e7));
    f.lonE7 = static_cast<int32_t>(std::llround(r.lonDeg * 1e7));
    if (r.altM && std::isfinite(*r.altM)) {
        f.altM = clampRound<int16_t>(*r.altM, -32768, 32767);
        f.flags |= Fix::kAlt;
    }
    if (r.speedKmh && std::isfinite(*r.speedKmh) && *r.speedKmh >= 0) {
        f.speedCkmh = clampRound<uint16_t>(*r.speedKmh * 100.0, 0, 65535);
        f.flags |= Fix::kSpeed;
    }
    if (r.courseDeg && std::isfinite(*r.courseDeg) && *r.courseDeg >= 0 && *r.courseDeg <= 360) {
        f.courseCdeg = clampRound<uint16_t>(*r.courseDeg * 100.0, 0, 36000);
        f.flags |= Fix::kCourse;
    }
    if (r.sats && *r.sats > 0) {
        f.sats = clampRound<uint8_t>(*r.sats, 0, 255);
        f.flags |= Fix::kSats;
    }
    if (r.hdop && std::isfinite(*r.hdop) && *r.hdop > 0) {
        f.hdopDeci = clampRound<uint8_t>(*r.hdop * 10.0, 1, 255);
        f.flags |= Fix::kHdop;
    }
    if (r.accuracyM && std::isfinite(*r.accuracyM) && *r.accuracyM > 0) {
        f.accM = clampRound<uint16_t>(*r.accuracyM, 1, 65535);
        f.flags |= Fix::kAcc;
    }
    return f;
}

}  // namespace core
