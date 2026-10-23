#pragma once

#include <cstdint>
#include <optional>

namespace core {

// One GNSS reading as the receiver reports it, before any validation.
struct GnssReading {
    bool hasFix = false;  // receiver reports a 2D or 3D lock
    int64_t timeMs = 0;   // UTC epoch ms of the fix
    double latDeg = 0;
    double lonDeg = 0;
    std::optional<float> altM;
    std::optional<float> speedKmh;
    std::optional<float> courseDeg;
    std::optional<float> hdop;
    std::optional<int> sats;  // satellites used, all constellations
    // The receiver's own horizontal error estimate, metres at 1 sigma (NMEA GST).
    std::optional<float> accuracyM;
};

// A fix as stored in the outbox: 24 bytes, whole-second timestamp (docs/ARCHITECTURE.md §7).
struct Fix {
    enum Flags : uint8_t { kAlt = 1, kSpeed = 2, kCourse = 4, kSats = 8, kHdop = 16, kAcc = 32 };

    uint32_t tsS = 0;  // UTC epoch seconds; uint32 lasts until 2106
    int32_t latE7 = 0;
    int32_t lonE7 = 0;
    int16_t altM = 0;         // whole metres: GNSS vertical error is several metres anyway
    uint16_t speedCkmh = 0;   // 0.01 km/h
    uint16_t courseCdeg = 0;  // 0.01 degree
    uint8_t sats = 0;
    uint8_t hdopDeci = 0;  // 0.1, saturates at 25.5
    uint8_t flags = 0;     // which optional fields are present
    uint16_t accM = 0;     // receiver's error estimate, whole metres

    bool has(Flags f) const { return (flags & f) != 0; }
    // Drops speed and course: they are noise unless the fix is good (FixQuality.h).
    void clearMotion() { flags &= static_cast<uint8_t>(~(kSpeed | kCourse)); }
};

static_assert(sizeof(Fix) == 24, "Fix is sized for RAM/RTC storage; keep it at 24 bytes");

// Applies PROTOCOL.md's fix rules (receiver locked, not exactly 0,0, lat/lon in range,
// ts not before 2020) and packs the reading. nullopt means the fix must not be sent.
std::optional<Fix> fixFromReading(const GnssReading& r);

}  // namespace core
