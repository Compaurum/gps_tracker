#pragma once

#include <stdint.h>

#include "Nmea.h"
#include "drivers/AtChannel.h"
#include "drivers/gnss/GnssSource.h"

namespace drivers {

// The A7670E-FASE's built-in GNSS, driven over the shared AT channel.
// Needs an active antenna on the modem's GNSS connector.
//
// Main source: the NMEA stream (AT+CGNSSTST=1) with RMC, GGA and GST, picked out of the AT
// channel as URCs and assembled per epoch. NMEA sentences are short, complete and checksummed,
// unlike AT+CGNSSINFO, which this firmware cuts at 98 characters; GST adds the receiver's own
// error estimate in metres. If no NMEA arrives at all, read() falls back to polling CGNSSINFO.
class ModemGnss : public GnssSource, public UrcListener {
public:
    // GNSS constellations (AT+CGNSSMODE): 4 = GPS + BeiDou + Galileo + SBAS + QZSS. BeiDou is
    // strong over Australia; mode 3 (the default) uses GLONASS instead.
    static constexpr int kGnssMode = 4;

    explicit ModemGnss(AtChannel& at);

    bool powerOn() override;
    void powerOff() override;
    bool isOn() const override { return on_; }
    void markOff() override { on_ = false; }
    std::optional<core::GnssReading> read() override;
    bool assist() override;

    bool onUrc(const char* line) override;

private:
    void configureOutput();
    std::optional<core::GnssReading> readCgnssInfo();

    AtChannel& at_;
    bool on_ = false;
    core::NmeaAssembler nmea_;
    uint32_t configuredAtMs_ = 0;
    bool fallbackLogged_ = false;
    bool gstLogged_ = false;
};

}  // namespace drivers
