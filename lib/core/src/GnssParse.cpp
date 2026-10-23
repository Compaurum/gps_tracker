#include "GnssParse.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

#include "AtParse.h"
#include "ClockMath.h"

namespace core {

namespace {

constexpr size_t kMaxFields = 24;
constexpr size_t kTruncatedLength = 98;
constexpr double kKnotsToKmh = 1.852;

enum Field {
    kMode = 0,
    kGpsSv,
    kBdsSv,
    kGloSv,
    kGalSv,
    kLat,
    kNs,
    kLon,
    kEw,
    kDate,
    kTime,
    kAlt,
    kSpeed,
    kCourse,
    kPdop,
    kHdop,
    kVdop,
};

std::optional<double> number(const char* s) {
    if (!s || !*s) return std::nullopt;
    char* end = nullptr;
    const double v = strtod(s, &end);
    if (end == s || *end != '\0') return std::nullopt;
    return v;
}

bool digits(const char* s, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (!isdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    return true;
}

int twoDigits(const char* s) { return (s[0] - '0') * 10 + (s[1] - '0'); }

// ddmmyy + hhmmss[.ss] → epoch ms.
std::optional<int64_t> fixTime(const char* date, const char* time) {
    if (strlen(date) != 6 || !digits(date, 6) || strlen(time) < 6 || !digits(time, 6)) return std::nullopt;
    auto ms = epochMsFromUtc(2000 + twoDigits(date + 4), twoDigits(date + 2), twoDigits(date), twoDigits(time),
                             twoDigits(time + 2), twoDigits(time + 4));
    if (!ms) return std::nullopt;
    if (time[6] == '.') {
        auto frac = number(time + 6);  // ".25" → 0.25
        if (!frac) return std::nullopt;
        *ms += static_cast<int64_t>(*frac * 1000.0 + 0.5);
    } else if (time[6] != '\0') {
        return std::nullopt;
    }
    return ms;
}

std::optional<float> optFloat(const char* s) {
    auto v = number(s);
    if (!v) return std::nullopt;
    return static_cast<float>(*v);
}

}  // namespace

std::optional<GnssReading> parseCgnssInfo(const char* line) {
    static const char kPrefix[] = "+CGNSSINFO:";
    if (!startsWith(line, kPrefix)) return std::nullopt;

    char buf[256];
    strncpy(buf, line + sizeof(kPrefix) - 1, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    // Split on commas, keeping empty fields; trim spaces around each.
    const char* fields[kMaxFields] = {};
    size_t n = 0;
    char* p = buf;
    while (n < kMaxFields) {
        while (*p == ' ') ++p;
        fields[n++] = p;
        char* comma = strchr(p, ',');
        char* end = comma ? comma : p + strlen(p);
        while (end > p && end[-1] == ' ') --end;
        const bool last = comma == nullptr;
        *end = '\0';
        if (last) break;
        p = comma + 1;
    }

    GnssReading r;
    auto mode = number(fields[kMode]);
    if (!mode || (*mode != 2 && *mode != 3)) return r;  // no lock (empty or mode 1)
    if (n <= kTime) return std::nullopt;
    // A line cut at the modem's limit may end in a shortened field; don't trust that one.
    if (strlen(line) >= kTruncatedLength) fields[--n] = "";
    for (size_t i = n; i < kMaxFields; ++i) fields[i] = "";

    auto lat = number(fields[kLat]);
    auto lon = number(fields[kLon]);
    const char ns = fields[kNs][0];
    const char ew = fields[kEw][0];
    auto t = fixTime(fields[kDate], fields[kTime]);
    if (!lat || !lon || (ns != 'N' && ns != 'S') || (ew != 'E' && ew != 'W') || !t) return std::nullopt;

    r.hasFix = true;
    r.timeMs = *t;
    r.latDeg = ns == 'S' ? -*lat : *lat;
    r.lonDeg = ew == 'W' ? -*lon : *lon;
    r.altM = optFloat(fields[kAlt]);
    if (auto knots = number(fields[kSpeed])) r.speedKmh = static_cast<float>(*knots * kKnotsToKmh);
    r.courseDeg = optFloat(fields[kCourse]);
    r.hdop = optFloat(fields[kHdop]);
    int sats = 0;
    bool anySats = false;
    for (int f = kGpsSv; f <= kGalSv; ++f) {
        if (auto v = number(fields[f])) {
            sats += static_cast<int>(*v);
            anySats = true;
        }
    }
    if (anySats) r.sats = sats;
    return r;
}

}  // namespace core
