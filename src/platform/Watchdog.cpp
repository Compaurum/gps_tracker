#include "Watchdog.h"

#include <esp_task_wdt.h>

namespace platform::watchdog {

static bool sEnabled = false;

void begin(uint32_t timeoutS) {
    // IDF 4.4: re-initialising an already running TWDT just updates timeout and panic mode.
    esp_task_wdt_init(timeoutS, true);
    esp_task_wdt_add(nullptr);
    sEnabled = true;
}

void feed() {
    if (sEnabled) esp_task_wdt_reset();
}

}  // namespace platform::watchdog
