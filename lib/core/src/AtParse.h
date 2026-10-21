#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

// Parsers for single A7670 response lines. Pure, so the odd formats are unit-tested off-device.
namespace core {

// True if `line` starts with `prefix`.
bool startsWith(const char* line, const char* prefix);

// Reads up to `max` comma-separated integers that follow `prefix`, e.g. "+CMQTTPUB: 0,0".
// Returns how many were read; stops at the first field that isn't an integer.
size_t parseInts(const char* line, const char* prefix, int* out, size_t max);

// "+CCLK: \"26/10/21,04:20:48+40\"" → UTC epoch ms. The modem reports local time plus the
// offset in quarter-hours, so the offset is subtracted. Does not check plausibility.
std::optional<int64_t> parseCclk(const char* line);

// "+CSQ: <rssi>,<ber>" → dBm. rssi 99 means "unknown" → nullopt.
std::optional<int> parseCsqDbm(const char* line);

// "+CEREG: <n>,<stat>" / "+CREG: ..." → true when registered (home 1 or roaming 5).
// `stat` (if not null) receives the raw status so callers can log searching / denied.
bool parseRegistered(const char* line, const char* prefix, int* stat);

}  // namespace core
