// One-off provisioning firmware (env T-Call-A7670X-V1-0-bridge-provision).
// Writes the device's AWS credentials into the modem's certificate store, where the normal
// firmware references them by file name, then turns into an AT pass-through shell.
// This image contains the private key: erase flash afterwards (README → Provisioning).
#include <Arduino.h>

#include "config/DeviceConfig.h"
#include "drivers/A7670Modem.h"
#include "drivers/AtChannel.h"
#include "platform/Board.h"
#include "platform/Log.h"
#include "provision_certs.h"  // generated into the build dir by scripts/embed_certs.py

static const char* const TAG = "PROVISION";

static bool provision(drivers::A7670Modem& modem) {
    struct Item {
        const char* file;
        const char* pem;
    };
    const Item items[] = {
        {config::kCaCertFile, kProvisionCaPem},
        {config::kClientCertFile, kProvisionCertPem},
        {config::kClientKeyFile, kProvisionKeyPem},
    };
    for (const Item& it : items) {
        if (!modem.uploadCertificate(it.file, it.pem)) return false;
    }
    for (const Item& it : items) {
        if (!modem.hasCertificate(it.file)) {
            LOGE(TAG, "%s not listed after upload", it.file);
            return false;
        }
    }
    return true;
}

void setup() {
    platform::logBegin(115200);
    platform::board::initPins();
    LOGI(TAG, "device %s", config::kDeviceId);

    static drivers::AtChannel at(platform::board::modemSerial());
    at.setTrace(true);  // safe: certificate bytes are never traced
    static drivers::A7670Modem modem(at);
    if (!modem.powerOn()) {
        LOGE(TAG, "modem not answering");
    } else if (provision(modem)) {
        LOGI(TAG, "done: certificates stored in the modem");
        LOGI(TAG, "next: erase flash, then upload the normal firmware (README → Provisioning)");
    } else {
        LOGE(TAG, "FAILED, see errors above");
    }
    LOGI(TAG, "AT pass-through: type AT commands, end lines with CR");
}

void loop() {
    HardwareSerial& modem = platform::board::modemSerial();
    while (modem.available()) Serial.write(modem.read());
    while (Serial.available()) modem.write(Serial.read());
}
