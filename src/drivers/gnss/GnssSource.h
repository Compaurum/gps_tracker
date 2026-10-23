#pragma once

#include <optional>

#include "Fix.h"

namespace drivers {

// A GNSS receiver. Today the A7670's built-in one; an external module would be another
// implementation (docs/ARCHITECTURE.md §2.2).
class GnssSource {
public:
    virtual ~GnssSource() = default;

    virtual bool powerOn() = 0;
    virtual void powerOff() = 0;
    virtual bool isOn() const = 0;
    // Forget the power state, e.g. after the modem hosting the receiver was reset.
    virtual void markOff() = 0;
    // The receiver's current solution. hasFix == false while it has no lock.
    // nullopt if the receiver couldn't be read at all (silent, or not answering).
    virtual std::optional<core::GnssReading> read() = 0;
    // Downloads assistance data (orbits) over the data link to shorten a cold start.
    // Needs the cellular data bearer up. False if unsupported or it failed.
    virtual bool assist() { return false; }
};

}  // namespace drivers
