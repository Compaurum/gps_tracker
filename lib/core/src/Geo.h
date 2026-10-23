#pragma once

namespace core {

// Ground distance in metres between two WGS84 points. Equirectangular approximation: well under
// 1% error for the few-kilometre distances motion detection compares.
double distanceM(double lat1Deg, double lon1Deg, double lat2Deg, double lon2Deg);

}  // namespace core
