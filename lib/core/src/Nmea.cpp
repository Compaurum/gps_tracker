#include "Nmea.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "ClockMath.h"

namespace core {

namespace {

constexpr int kMaxFields = 24;
constexpr double kKnotsToKmh = 1.852;

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

std::optional<double> number(const char* s) {
    if (!s || !*s) return std::nullopt;
    char* end = nullptr;
    const double v = strtod(s, &end);
    if (end == s || *end != '\0' || !std::isfinite(v)) return std::nullopt;
    return v;
}

std::optional<float> optFloat(const char* s) {
    auto v = number(s);
    if (!v) return std::nullopt;
    return static_cast<float>(*v);
}

// NMEA angles are [d]ddmm.mmmm; hemisphere 'S' / 'W' makes them negative.
std::optional<double> angle(const char* value, const char* hemi, char negative, char positive) {
    auto v = number(value);
    if (!v || !hemi || (hemi[0] != negative && hemi[0] != positive)) return std::nullopt;
    const double deg = std::floor(*v / 100.0);
    const double minutes = *v - deg * 100.0;
    if (minutes < 0 || minutes >= 60) return std::nullopt;
    const double d = deg + minutes / 60.0;
    return hemi[0] == negative ? -d : d;
}

bool digits(const char* s, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (!isdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    return true;
}

int twoDigits(const char* s) { return (s[0] - '0') * 10 + (s[1] - '0'); }

// ddmmyy + hhmmss[.ss] → epoch ms.
std::optional<int64_t> utcMs(const char* date, const char* time) {
    if (strlen(date) != 6 || !digits(date, 6) || strlen(time) < 6 || !digits(time, 6)) return std::nullopt;
    auto ms = epochMsFromUtc(2000 + twoDigits(date + 4), twoDigits(date + 2), twoDigits(date), twoDigits(time),
                             twoDigits(time + 2), twoDigits(time + 4));
    if (!ms) return std::nullopt;
    if (time[6] == '.') {
        auto frac = number(time + 6);
        if (!frac) return std::nullopt;
        *ms += static_cast<int64_t>(*frac * 1000.0 + 0.5);
    } else if (time[6] != '\0') {
        return std::nullopt;
    }
    return ms;
}

void copyTime(char* dst, const char* src) {
    strncpy(dst, src, 11);
    dst[11] = '\0';
}

}  // namespace

bool nmeaChecksumOk(const char* line) {
    if (!line || line[0] != '$') return false;
    const char* star = strrchr(line, '*');
    if (!star || strlen(star) < 3) return false;
    uint8_t sum = 0;
    for (const char* p = line + 1; p < star; ++p) sum ^= static_cast<uint8_t>(*p);
    const int hi = hexDigit(star[1]);
    const int lo = hexDigit(star[2]);
    return hi >= 0 && lo >= 0 && sum == static_cast<uint8_t>(hi * 16 + lo);
}

bool NmeaAssembler::feed(const char* line, uint32_t nowMs) {
    if (!line || line[0] != '$') return false;
    anySentence_ = true;
    if (!nmeaChecksumOk(line)) {
        ++badChecksums_;
        return true;
    }
    // "$GNRMC,..." → talker (2 chars) + type (3 chars); split the body (without "*hh") on commas.
    char buf[128];
    strncpy(buf, line + 1, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char* star = strrchr(buf, '*');
    if (star) *star = '\0';
    char* f[kMaxFields] = {};
    int n = 0;
    char* p = buf;
    while (n < kMaxFields) {
        f[n++] = p;
        char* comma = strchr(p, ',');
        if (!comma) break;
        *comma = '\0';
        p = comma + 1;
    }
    if (strlen(f[0]) != 5) return true;
    const char* type = f[0] + 2;
    if (strcmp(type, "RMC") == 0) {
        parseRmc(f, n, nowMs);
    } else if (strcmp(type, "GGA") == 0) {
        parseGga(f, n);
    } else if (strcmp(type, "GST") == 0) {
        parseGst(f, n);
    }
    return true;
}

// $xxRMC,hhmmss.ss,A,ddmm.mm,N,dddmm.mm,E,knots,course,ddmmyy,magvar,E,mode[,navstatus]
bool NmeaAssembler::parseRmc(char** f, int n, uint32_t nowMs) {
    if (n < 10) return false;
    Rmc r;
    r.present = true;
    copyTime(r.time, f[1]);
    r.receivedMs = nowMs;
    // Mode (NMEA 2.3+): N = no fix, E = dead reckoning (an estimate, not a measurement).
    const char mode = n > 12 ? f[12][0] : 'A';
    const bool statusOk = f[2][0] == 'A' && mode != 'N' && mode != 'E';
    auto lat = angle(f[3], f[4], 'S', 'N');
    auto lon = angle(f[5], f[6], 'W', 'E');
    auto t = utcMs(f[9], f[1]);
    if (statusOk && lat && lon && t) {
        r.valid = true;
        r.latDeg = *lat;
        r.lonDeg = *lon;
        r.timeMs = *t;
        if (auto knots = number(f[7])) r.speedKmh = static_cast<float>(*knots * kKnotsToKmh);
        r.courseDeg = optFloat(f[8]);
    }
    rmc_ = r;
    return r.valid;
}

// $xxGGA,hhmmss.ss,lat,N,lon,E,quality,sats,hdop,alt,M,geoid,M,age,station
bool NmeaAssembler::parseGga(char** f, int n) {
    if (n < 10) return false;
    Gga g;
    g.present = true;
    copyTime(g.time, f[1]);
    g.quality = atoi(f[6]);
    if (auto s = number(f[7])) g.sats = static_cast<int>(*s);
    g.hdop = optFloat(f[8]);
    g.altM = optFloat(f[9]);
    gga_ = g;
    return true;
}

// $xxGST,hhmmss.ss,rms,semimajor,semiminor,orientation,lat_err,lon_err,alt_err (metres, 1 sigma)
bool NmeaAssembler::parseGst(char** f, int n) {
    if (n < 8) return false;
    Gst g;
    g.present = true;
    copyTime(g.time, f[1]);
    auto latErr = number(f[6]);
    auto lonErr = number(f[7]);
    if (latErr && lonErr && *latErr >= 0 && *lonErr >= 0) {
        g.errorM = static_cast<float>(std::sqrt(*latErr * *latErr + *lonErr * *lonErr));
        gstSeen_ = true;
    }
    gst_ = g;
    return g.errorM.has_value();
}

std::optional<GnssReading> NmeaAssembler::latest(uint32_t nowMs, uint32_t maxAgeMs) const {
    if (!rmc_.present || nowMs - rmc_.receivedMs > maxAgeMs) return std::nullopt;
    GnssReading r;
    if (!rmc_.valid) return r;

    const bool ggaMatches = gga_.present && strcmp(gga_.time, rmc_.time) == 0;
    // GGA quality: 0 invalid, 6 dead reckoning, 7 manual input, 8 simulator.
    if (ggaMatches && (gga_.quality == 0 || gga_.quality >= 6)) return r;

    r.hasFix = true;
    r.timeMs = rmc_.timeMs;
    r.latDeg = rmc_.latDeg;
    r.lonDeg = rmc_.lonDeg;
    r.speedKmh = rmc_.speedKmh;
    r.courseDeg = rmc_.courseDeg;
    if (ggaMatches) {
        r.sats = gga_.sats;
        r.hdop = gga_.hdop;
        r.altM = gga_.altM;
    }
    if (gst_.present && strcmp(gst_.time, rmc_.time) == 0) r.accuracyM = gst_.errorM;
    return r;
}

}  // namespace core
