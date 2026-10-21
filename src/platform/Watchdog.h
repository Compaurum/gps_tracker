#pragma once

#include <cstdint>

// Task watchdog on the Arduino loop task. Every blocking wait must call feed(); a hang resets
// the board and the reset reason goes out in the next boot event.
namespace platform::watchdog {

void begin(uint32_t timeoutS);
void feed();

}  // namespace platform::watchdog
