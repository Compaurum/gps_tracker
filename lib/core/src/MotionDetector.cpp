#include "MotionDetector.h"

#include "Geo.h"

namespace core {

float MotionDetector::radiusFor(float errorM) const {
    // A fix whose own error is large must move correspondingly further to count.
    const float byError = 2.0f * errorM;
    return byError > cfg_.moveRadiusM ? byError : cfg_.moveRadiusM;
}

MotionDetector::Change MotionDetector::update(const Input& in) {
    if (in.quality == FixQuality::Poor) return Change::None;
    const float radius = radiusFor(in.errorM);

    if (!moving_) {
        if (!haveAnchor_) {
            anchorLat_ = in.latDeg;
            anchorLon_ = in.lonDeg;
            haveAnchor_ = true;
            return Change::None;
        }
        const bool outside = distanceM(anchorLat_, anchorLon_, in.latDeg, in.lonDeg) > radius;
        const bool fast = in.quality == FixQuality::Good && in.speedKmh && *in.speedKmh >= cfg_.movingSpeedKmh;
        outsideInRow_ = outside ? outsideInRow_ + 1 : 0;
        fastInRow_ = fast ? fastInRow_ + 1 : 0;
        if (outsideInRow_ >= cfg_.movingSamples || fastInRow_ >= cfg_.movingSamples) {
            moving_ = true;
            outsideInRow_ = fastInRow_ = 0;
            refLat_ = in.latDeg;
            refLon_ = in.lonDeg;
            lastProgressMs_ = in.nowMs;
            return Change::BecameMoving;
        }
        return Change::None;
    }

    if (distanceM(refLat_, refLon_, in.latDeg, in.lonDeg) > radius) {
        refLat_ = in.latDeg;
        refLon_ = in.lonDeg;
        lastProgressMs_ = in.nowMs;
    }
    if (in.nowMs - lastProgressMs_ >= cfg_.stationaryAfterMs) {
        moving_ = false;
        // Park here: later fixes are compared with where the bike actually stopped.
        anchorLat_ = in.latDeg;
        anchorLon_ = in.lonDeg;
        return Change::BecameStationary;
    }
    return Change::None;
}

}  // namespace core
