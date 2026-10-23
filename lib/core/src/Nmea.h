#pragma once

#include <cstdint>
#include <optional>

#include "Fix.h"

namespace core {

// True if `line` is "$...*hh" and hh matches the XOR of the characters in between.
bool nmeaChecksumOk(const char* line);

// Assembles one GnssReading per receiver epoch from the NMEA sentences the A7670 streams with
// AT+CGNSSTST=1 (talker GP, GN, GL, ... are all accepted):
//   RMC: time, date, validity, position, speed, course
//   GGA: fix quality, satellites used, HDOP, altitude
//   GST: error estimate per axis, metres at 1 sigma (if the receiver provides it)
// GGA and GST join the reading only when their UTC time matches the RMC's, so values from
// different epochs never mix. Sentences with a bad checksum are counted and dropped.
class NmeaAssembler {
public:
    // Returns true if the line was an NMEA sentence (whether or not it was usable).
    bool feed(const char* line, uint32_t nowMs);

    // The newest epoch, if its RMC arrived within maxAgeMs. nullopt means the receiver is
    // silent. A reading with hasFix == false means it talks but has no position.
    std::optional<GnssReading> latest(uint32_t nowMs, uint32_t maxAgeMs) const;

    bool anySentence() const { return anySentence_; }
    bool gstSeen() const { return gstSeen_; }
    uint32_t badChecksums() const { return badChecksums_; }

private:
    struct Rmc {
        bool present = false;
        char time[12] = {};
        uint32_t receivedMs = 0;
        bool valid = false;
        int64_t timeMs = 0;
        double latDeg = 0;
        double lonDeg = 0;
        std::optional<float> speedKmh;
        std::optional<float> courseDeg;
    };
    struct Gga {
        bool present = false;
        char time[12] = {};
        int quality = 0;
        std::optional<int> sats;
        std::optional<float> hdop;
        std::optional<float> altM;
    };
    struct Gst {
        bool present = false;
        char time[12] = {};
        std::optional<float> errorM;
    };

    bool parseRmc(char** f, int n, uint32_t nowMs);
    bool parseGga(char** f, int n);
    bool parseGst(char** f, int n);

    Rmc rmc_;
    Gga gga_;
    Gst gst_;
    bool anySentence_ = false;
    bool gstSeen_ = false;
    uint32_t badChecksums_ = 0;
};

}  // namespace core
