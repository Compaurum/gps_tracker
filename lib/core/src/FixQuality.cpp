#include "FixQuality.h"

#include <cmath>

namespace core {

const char* fixQualityName(FixQuality q) {
    switch (q) {
        case FixQuality::Good: return "good";
        case FixQuality::Fair: return "fair";
        case FixQuality::Poor: return "poor";
    }
    return "?";
}

Accuracy assessAccuracy(const GnssReading& r, const TrackerConfig& cfg) {
    Accuracy a;
    if (!r.hasFix) return a;
    if (r.sats && *r.sats < cfg.minSatellites) return a;

    if (r.accuracyM && std::isfinite(*r.accuracyM) && *r.accuracyM > 0) {
        a.errorM = *r.accuracyM;
        a.fromReceiver = true;
        if (a.errorM <= cfg.goodAccuracyM) {
            a.quality = FixQuality::Good;
        } else if (a.errorM <= cfg.fairAccuracyM) {
            a.quality = FixQuality::Fair;
        }
        return a;
    }
    if (r.hdop && std::isfinite(*r.hdop) && *r.hdop > 0) {
        a.errorM = *r.hdop * cfg.hdopToMetres;
        if (a.errorM <= cfg.fairAccuracyM) a.quality = FixQuality::Fair;
    }
    return a;
}

}  // namespace core
