#pragma once

#include <cstddef>
#include <cstdint>

namespace core {

// Tracking tunables. Defaults follow PROTOCOL.md's suggested cadence; set_interval (cmd) will
// override sample/publish intervals later.
struct TrackerConfig {
    uint32_t sampleIntervalMs = 10000;
    uint32_t publishIntervalMs = 60000;
    // While stationary only a heartbeat goes out. Any delivered report resets this clock.
    uint32_t heartbeatIntervalMs = 15UL * 60 * 1000;
    // After a failed publish, wait this long before trying again.
    uint32_t retryAfterFailMs = 30000;
    // Protocol limit per telemetry message.
    size_t maxFixesPerMessage = 100;

    // Fix quality (FixQuality.h), from the receiver's own horizontal error estimate (NMEA GST,
    // 1 sigma) when it gives one. Only "good" fixes have trustworthy speed and course; "poor"
    // fixes are neither sent nor used for motion.
    float goodAccuracyM = 25.0f;
    float fairAccuracyM = 100.0f;
    // Without GST, the error is estimated as HDOP x this. HDOP knows nothing about multipath, so
    // such fixes are never rated better than "fair".
    float hdopToMetres = 10.0f;
    // Fewer satellites than this cannot give a 3D position.
    int minSatellites = 4;

    // Motion without an accelerometer (MotionDetector.h): moving once `movingSamples` usable
    // fixes in a row lie farther than moveRadiusM from where the bike was parked, or as many good
    // fixes in a row report at least movingSpeedKmh. Stationary once no fix has moved farther
    // than moveRadiusM for stationaryAfterMs. The radius sits above the ~90 m multipath jumps
    // seen next to a window.
    float moveRadiusM = 150.0f;
    uint8_t movingSamples = 2;
    float movingSpeedKmh = 10.0f;
    uint32_t stationaryAfterMs = 5UL * 60 * 1000;
};

}  // namespace core
