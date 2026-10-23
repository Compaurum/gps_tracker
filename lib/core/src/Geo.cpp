#include "Geo.h"

#include <cmath>

namespace core {

double distanceM(double lat1Deg, double lon1Deg, double lat2Deg, double lon2Deg) {
    constexpr double kEarthRadiusM = 6371000.0;
    constexpr double kRad = 3.14159265358979323846 / 180.0;
    double dLon = (lon2Deg - lon1Deg) * kRad;
    // Take the short way round across the antimeridian.
    if (dLon > 3.14159265358979323846) dLon -= 2 * 3.14159265358979323846;
    if (dLon < -3.14159265358979323846) dLon += 2 * 3.14159265358979323846;
    const double x = dLon * std::cos((lat1Deg + lat2Deg) / 2 * kRad);
    const double y = (lat2Deg - lat1Deg) * kRad;
    return std::sqrt(x * x + y * y) * kEarthRadiusM;
}

}  // namespace core
