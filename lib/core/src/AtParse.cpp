#include "AtParse.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

#include "ClockMath.h"

namespace core {

bool startsWith(const char* line, const char* prefix) {
    return line && prefix && strncmp(line, prefix, strlen(prefix)) == 0;
}

size_t parseInts(const char* line, const char* prefix, int* out, size_t max) {
    if (!startsWith(line, prefix)) return 0;
    const char* p = line + strlen(prefix);
    size_t n = 0;
    while (n < max) {
        while (*p == ' ') ++p;
        char* end = nullptr;
        long v = strtol(p, &end, 10);
        if (end == p) break;
        out[n++] = static_cast<int>(v);
        p = end;
        while (*p == ' ') ++p;
        if (*p != ',') break;
        ++p;
    }
    return n;
}

// Reads exactly two decimal digits.
static bool twoDigits(const char* p, int* out) {
    if (!isdigit(static_cast<unsigned char>(p[0])) || !isdigit(static_cast<unsigned char>(p[1]))) return false;
    *out = (p[0] - '0') * 10 + (p[1] - '0');
    return true;
}

std::optional<int64_t> parseCclk(const char* line) {
    if (!startsWith(line, "+CCLK:")) return std::nullopt;
    const char* q = strchr(line, '"');
    if (!q) return std::nullopt;
    const char* p = q + 1;
    // yy/MM/dd,hh:mm:ss±zz  (zz = quarter hours, one or two digits)
    int yy, mo, dd, hh, mi, ss;
    if (!twoDigits(p, &yy) || p[2] != '/' || !twoDigits(p + 3, &mo) || p[5] != '/' ||
        !twoDigits(p + 6, &dd) || p[8] != ',' || !twoDigits(p + 9, &hh) || p[11] != ':' ||
        !twoDigits(p + 12, &mi) || p[14] != ':' || !twoDigits(p + 15, &ss)) {
        return std::nullopt;
    }
    int quarters = 0;
    const char sign = p[17];
    if (sign == '+' || sign == '-') {
        char* end = nullptr;
        long z = strtol(p + 18, &end, 10);
        if (end == p + 18) return std::nullopt;
        quarters = static_cast<int>(sign == '-' ? -z : z);
    } else if (sign != '"') {
        return std::nullopt;
    }
    auto local = epochMsFromUtc(2000 + yy, mo, dd, hh, mi, ss);
    if (!local) return std::nullopt;
    return *local - static_cast<int64_t>(quarters) * 15 * 60 * 1000;
}

std::optional<int> parseCsqDbm(const char* line) {
    int v[2];
    if (parseInts(line, "+CSQ:", v, 2) < 1) return std::nullopt;
    if (v[0] < 0 || v[0] > 31) return std::nullopt;  // 99 = not known or not detectable
    return -113 + 2 * v[0];
}

bool parseRegistered(const char* line, const char* prefix, int* stat) {
    int v[2];
    if (parseInts(line, prefix, v, 2) < 2) return false;
    if (stat) *stat = v[1];
    return v[1] == 1 || v[1] == 5;
}

}  // namespace core
