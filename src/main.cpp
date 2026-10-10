#include <Arduino.h>
#include "utilities.h"

#define TINY_GSM_RX_BUFFER 1024
#define SerialMon Serial
#define TINY_GSM_DEBUG SerialMon
// #define DUMP_AT_COMMANDS

#include <TinyGsmClient.h>

#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, SerialMon);
TinyGsm modem(debugger);
#else
TinyGsm modem(SerialAT);
#endif

static void pulsePowerKey() {
    digitalWrite(BOARD_PWRKEY_PIN, LOW);
    delay(100);
    digitalWrite(BOARD_PWRKEY_PIN, HIGH);
    delay(MODEM_POWERON_PULSE_WIDTH_MS);
    digitalWrite(BOARD_PWRKEY_PIN, LOW);
}

void setup() {
    SerialMon.begin(115200);

    pinMode(MODEM_RESET_PIN, OUTPUT);
    digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL); delay(100);
    digitalWrite(MODEM_RESET_PIN, MODEM_RESET_LEVEL); delay(2600);
    digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);

    // Keep DTR low so the modem doesn't enter sleep
    pinMode(MODEM_DTR_PIN, OUTPUT);
    digitalWrite(MODEM_DTR_PIN, LOW);

    pinMode(BOARD_PWRKEY_PIN, OUTPUT);
    pulsePowerKey();

    SerialAT.begin(MODEM_BAUDRATE, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

    SerialMon.println("Start modem...");
    delay(3000);

    int retry = 0;
    while (!modem.testAT(1000)) {
        SerialMon.print(".");
        if (retry++ > 30) {
            pulsePowerKey();
            retry = 0;
        }
    }
    SerialMon.println();
    SerialMon.print("Modem: ");
    SerialMon.println(modem.getModemName());
}

void loop() {
    // Pass-through so AT commands can be typed in the serial monitor
    while (SerialAT.available()) SerialMon.write(SerialAT.read());
    while (SerialMon.available()) SerialAT.write(SerialMon.read());
}
