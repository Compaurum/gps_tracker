#pragma once

#include <cstdint>
#include <optional>

// Calendar and epoch arithmetic. Wire timestamps are UTC epoch milliseconds (docs/ARCHITECTURE.md §7).
namespace core {

// 2020-01-01T00:00:00Z. The backend treats anything earlier as "clock not set".
constexpr int64_t kMinPlausibleEpochMs = 1577836800000LL;

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's algorithm).
int64_t daysFromCivil(int year, unsigned month, unsigned day);

// UTC calendar fields → epoch ms. nullopt if a field is out of range.
std::optional<int64_t> epochMsFromUtc(int year, int month, int day, int hour, int minute, int second);

}  // namespace core
