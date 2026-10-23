#pragma once

#include <optional>

#include "Fix.h"

namespace core {

// Parses the A7670's AT+CGNSSINFO answer:
//   +CGNSSINFO: <mode>,<GPS-SVs>,<BDS-SVs>,<GLONASS-SVs>,<GALILEO-SVs>,<lat>,<N/S>,<lon>,<E/W>,
//               <ddmmyy>,<hhmmss.ss>,<alt m>,<speed knots>,<course>,<PDOP>,<HDOP>,<VDOP>,...
// lat/lon are decimal degrees on the A76xx. Without a lock the fields are empty, which yields a
// reading with hasFix == false. nullopt only for a line that isn't a CGNSSINFO answer at all or
// claims a lock but is malformed.
//
// Fallback only: the A7670 (A011B07) cuts this answer at 98 characters, so the fields after
// the time may be missing or cut short. Anything from altitude on is optional, and the last
// field of a 98-character line is ignored as possibly shortened. NMEA is the main source.
std::optional<GnssReading> parseCgnssInfo(const char* line);

}  // namespace core
