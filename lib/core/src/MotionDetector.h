#pragma once

#include <cstdint>
#include <optional>

#include "Config.h"
#include "FixQuality.h"

namespace core {

// Moving / stationary from position, the stand-in for an accelerometer on this hardware version
// (docs/ARCHITECTURE.md D8). Distance travelled, not reported speed, is the main evidence: a
// parked receiver's position wanders around one spot (tens of metres with multipath), while a
// moving bike makes progress. Speed counts only from good fixes. Poor fixes are ignored.
// Starts stationary; the first usable fix becomes the parking anchor.
class MotionDetector {
public:
    enum class Change { None, BecameMoving, BecameStationary };

    struct Input {
        uint32_t nowMs = 0;
        double latDeg = 0;
        double lonDeg = 0;
        FixQuality quality = FixQuality::Poor;
        float errorM = 0;
        std::optional<float> speedKmh;  // only from good fixes
    };

    explicit MotionDetector(const TrackerConfig& cfg) : cfg_(cfg) {}

    Change update(const Input& in);
    bool moving() const { return moving_; }

private:
    float radiusFor(float errorM) const;

    const TrackerConfig& cfg_;
    bool moving_ = false;
    bool haveAnchor_ = false;
    double anchorLat_ = 0;
    double anchorLon_ = 0;
    uint8_t outsideInRow_ = 0;
    uint8_t fastInRow_ = 0;
    // While moving: the last point that was a full radius away from the one before.
    double refLat_ = 0;
    double refLon_ = 0;
    uint32_t lastProgressMs_ = 0;
};

}  // namespace core
