#pragma once

#include <HardwareSerial.h>

// LilyGO T-Call A7670 V1.0 wiring. The only place that includes utilities.h.
namespace platform::board {

void initPins();

// UART to the modem, opened with the board's pins and baud rate.
HardwareSerial& modemSerial();

// Hardware reset followed by a PWRKEY pulse, as in LilyGO's examples. Blocks ~3 s.
void modemPowerOnSequence();
// PWRKEY pulse alone. Toggles the modem on <-> off.
void modemPowerKeyPulse();

void setLed(bool on);

// Why the ESP32 last reset, as a short lowercase word for the boot event ("poweron", "panic", …).
const char* resetReason();

}  // namespace platform::board
