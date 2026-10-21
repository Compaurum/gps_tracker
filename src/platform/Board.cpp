#include "Board.h"

#include <Arduino.h>
#include <esp_system.h>

#include "utilities.h"

namespace platform::board {

// The default 256-byte RX buffer overflows while the loop is busy printing logs.
static constexpr size_t kModemRxBufferBytes = 4096;

void initPins() {
    pinMode(BOARD_LED_PIN, OUTPUT);
    pinMode(MODEM_RESET_PIN, OUTPUT);
    digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
    pinMode(BOARD_PWRKEY_PIN, OUTPUT);
    digitalWrite(BOARD_PWRKEY_PIN, LOW);
    // DTR low keeps the modem awake; sleep via DTR comes with the sleep lifecycle (P3).
    pinMode(MODEM_DTR_PIN, OUTPUT);
    digitalWrite(MODEM_DTR_PIN, LOW);
    pinMode(MODEM_RING_PIN, INPUT_PULLUP);
}

HardwareSerial& modemSerial() {
    static bool started = false;
    if (!started) {
        SerialAT.setRxBufferSize(kModemRxBufferBytes);
        SerialAT.begin(MODEM_BAUDRATE, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
        started = true;
    }
    return SerialAT;
}

void modemPowerKeyPulse() {
    digitalWrite(BOARD_PWRKEY_PIN, LOW);
    delay(100);
    digitalWrite(BOARD_PWRKEY_PIN, HIGH);
    delay(MODEM_POWERON_PULSE_WIDTH_MS);
    digitalWrite(BOARD_PWRKEY_PIN, LOW);
}

void modemPowerOnSequence() {
    digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
    delay(100);
    digitalWrite(MODEM_RESET_PIN, MODEM_RESET_LEVEL);
    delay(2600);
    digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
    modemPowerKeyPulse();
}

void setLed(bool on) {
    digitalWrite(BOARD_LED_PIN, on ? LED_ON : !LED_ON);
}

const char* resetReason() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON: return "poweron";
        case ESP_RST_EXT: return "external";
        case ESP_RST_SW: return "software";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: return "int_wdt";
        case ESP_RST_TASK_WDT: return "task_wdt";
        case ESP_RST_WDT: return "wdt";
        case ESP_RST_DEEPSLEEP: return "deepsleep";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_SDIO: return "sdio";
        default: return "unknown";
    }
}

}  // namespace platform::board
