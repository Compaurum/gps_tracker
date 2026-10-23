# gps_tracker

Firmware for a motorcycle GPS tracker on the LilyGO T-Call A7670 V1.0 (ESP32 + A7670E-FASE LTE Cat-1 with built-in GNSS).
It reports to the AWS IoT backend in `../gps_tracker_backend`, following that repo's `PROTOCOL.md` (v1).

- Design: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
- Power/state behaviour (draft, lower priority than the protocol): [Wake-on-Motion Lifecycle.md](Wake-on-Motion%20Lifecycle.md)

## Setup

1. Create `secrets.ini` in the project root (gitignored). The build reads it through `extra_configs`:

   ```ini
   [secrets]
   ; IoT thing name = MQTT client ID, [a-zA-Z0-9_-]{1,64}
   device_id = tracker-01
   ; aws iot describe-endpoint --endpoint-type iot:Data-ATS --region ap-southeast-2
   mqtt_host = xxxxxxxxxxxxxx-ats.iot.ap-southeast-2.amazonaws.com
   ; empty = the SIM's default APN
   apn =
   ; output folder of ../gps_tracker_backend/scripts/provision-device.sh (read only by the provision env)
   certs_dir = ../gps_tracker_backend/certs/tracker-01
   ```

   The firmware refuses to connect while `mqtt_host` still contains `CHANGE-ME`.

2. **Provisioning** (once per device, and again whenever the certificate changes). Enter download mode by hand
   (hold BOOT, press RST, release BOOT) before each upload. Press RST after it.

   ```sh
   pio run -e T-Call-A7670X-V1-0-bridge-provision -t upload   # writes the certs into the modem
   pio device monitor                                          # expect "PROVISION: done"
   pio run -e T-Call-A7670X-V1-0-bridge -t erase               # remove the image that contains the key
   pio run -e T-Call-A7670X-V1-0-bridge -t upload              # normal firmware
   ```

   The provisioning image embeds the private key, generated into `.pio/build/` by `scripts/embed_certs.py` and never
   into `src/`. After it runs, the key lives only in the modem's certificate store
   (`aws_root_ca.pem`, `device_cert.pem`, `device_key.pem`). The erase step makes sure no copy stays in ESP32 flash.
   Once provisioning is done, the provisioning firmware turns into an AT pass-through shell, which is handy for poking
   at the modem by hand.

3. **Run.** `pio device monitor` shows each state transition (`LIFE: NETWORK -> CLOCK ...`) and, with `-DAT_TRACE=1`
   (on by default in this phase), every AT line. A healthy boot ends with `UPLINK: status {...}` →
   `MQTT: PUBACK ...` → `LIFE: MQTT -> ONLINE (boot delivered)`.

## Tests

The pure logic in `lib/core` runs on the laptop:

```sh
pio test -e native
```

On macOS, if this fails with `'cstddef' file not found`, the Command Line Tools install is damaged:
`/Library/Developer/CommandLineTools/usr/include/c++/v1` holds a few leftover files that hide the SDK's libc++
headers. Reinstall the tools (`sudo rm -rf /Library/Developer/CommandLineTools && xcode-select --install`).
Until then, run the tests with the SDK headers added explicitly:

```sh
PLATFORMIO_BUILD_FLAGS="-isystem $(xcrun --show-sdk-path)/usr/include/c++/v1" pio test -e native
```

## Hardware roadmap

### Current version (this firmware)

- T-Call A7670 V1.0 on a 5 V bench supply, flashed through the ESP32-C3 USB-UART bridge (`T-Call-A7670X-V1-0-bridge` env).
- GNSS: the modem's built-in GNSS. Needs an **active** GNSS antenna (3.3 V bias) on the modem's GNSS IPEX connector.
- Wake sources: RTC timer and the modem RING pin (SMS / voice call). **No accelerometer**: the firmware uses a stub that
  reports "always moving", so the device tracks whenever it's awake.
- No bus-voltage sensing or self-disconnect yet. Fake implementations stand in for both.

### Next version: accelerometer

The chosen sensor is the **Analog Devices ADXL345** (3-axis, I²C, 2.0–3.6 V). Its activity interrupt (`THRESH_ACT`,
62.5 mg/LSB, with `ACT_INACT_CTL` picking the axes) drives INT1, and an RTC-capable GPIO lets it wake the ESP32 from
deep sleep (`ext1`). The INT pins are push-pull and active-high by default, which suits `ESP_EXT1_WAKEUP_ANY_HIGH`.
Activity detection only runs in measurement mode, about 23–40 µA (0.1 µA in standby). That's more than a LIS3DH /
LIS2DH12 (a few µA) but small next to the modem's idle draw. The firmware already has the `MotionSensor` interface;
adding the sensor means writing an ADXL345 driver and turning on motion-gated `GPS_ACQUIRE` and the idle timers.

- On our breakout SDO/ALT ADDRESS is tied low and CS is tied high (I²C mode), so the I²C address is **0x53**.
  `DEVID` (register 0x00) reads 0xE5.

Wiring, all on the right-hand header (modem side up, MAIN antenna at the top, pins counted from the top):

| Sensor | Board pin | Header pin | Note |
|---|---|---|---|
| VCC | 3V3 | 1 | |
| INT1 | GPIO34 | 5 | Input-only RTC pin, `ext1` wake. No internal pulls; the sensor drives the line. |
| INT2 | GPIO35 | 6 | Same as GPIO34. |
| SDA | GPIO32 | 7 | I²C needs output-capable pins, so not 34/35: `Wire.begin(32, 33)`. |
| SCL | GPIO33 | 8 | |
| GND | GND | 14 | Between IO12 and IO13. |

Header order comes from LilyGO's [T-Call-A7670 V1.0 schematic](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/schematic/esp32/T-Call-A7670-V1.0.pdf).
Don't use IO16/IO17 (WROVER PSRAM) or the strapping pins IO0/IO2/IO5/IO15. Still free: IO18, IO19, IO21, IO22, IO23,
GPIO36 (S_VP) and GPIO39 (S_VN).

### Bus-voltage detector and self-disconnect (recommendation)

The lifecycle doc needs two separate things. Keep them separate in hardware:

**1. Firmware measurement (tiers, `batV`, charging detection)**

- Divider from the switched 12 V bus to an **ADC1** pin. ADC2 is unusable while the radio is active. Use GPIO36
  (S_VP) or GPIO39 (S_VN): input-only ADC1 pins, free on the right-hand header. GPIO34/35 carry the accelerometer IRQs.
- **1 MΩ (top) / 180 kΩ (bottom) + 100 nF** from the ADC pin to GND. Full scale is about 16 V at 2.44 V on the pin, which is
  inside the ESP32's accurate range at 11 dB attenuation. The divider draws about 11 µA at 12.6 V. The capacitor is
  needed because the divider impedance is high for the ESP32 ADC.
- Firmware uses `analogReadMilliVolts()` (eFuse-calibrated) and averages 16 samples.
- Optionally add a 3.3 V Zener or clamp diode on the ADC pin to guard against load-dump spikes.

**2. Hardware cut-off (SELF_DISCONNECT): must work with the MCU powered off**

- **Supervisor:** Analog Devices **LTC2965** (3.5–100 V input, about 3.5 µA, separate rising and falling thresholds set
  with one resistor string). Set falling to **11.5 V** and rising to **12.2 V**, as in the lifecycle doc. A cheaper alternative
  is a nanopower comparator (e.g. TI TLV3691, about 75 nA) behind a low-Iq LDO, with a reference and resistor hysteresis.
- **Load switch:** high-side P-channel MOSFET (Vds ≥ 40 V, low Rds(on)) driven by a small NPN or N-MOSFET from
  the supervisor output, between the bike battery and the tracker's 5 V buck converter.
- **Crank-dip filter:** an RC delay on the supervisor output, or its built-in delay, so a 6–9 V dip for under 2 s while
  cranking doesn't trip the cut-off.
- **Firmware hook:** route the supervisor output (through a divider) to a spare GPIO as a "power-fail warning".
  Firmware then gets time to send `low_battery` / `shutdown` and save the outbox to NVS before the switch opens.
- **Supply chain around it:** fuse → reverse-polarity protection → TVS (load-dump) → load switch → 12 V→5 V buck
  rated ≥ 2.5 A (the modem peaks at about 2 A) with low quiescent current (e.g. TI LMR33630, about 25 µA Iq) → T-Call VBUS.

## Future improvements

- **Heartbeat interval.** It's 15 min now, per `PROTOCOL.md`. Move to **6 h (OK) / 12 h (LOW battery)** as in the lifecycle
  doc. Each heartbeat is a modem wake plus a TLS handshake, which costs the parked bike's battery. This needs a protocol
  change on the backend side: the "every 15 minutes" wording, and any staleness/offline alerting built on it.
- **Accelerometer wake-on-motion**: see Hardware roadmap → Next version.
- **Bus-voltage tiers and self-disconnect**: see Hardware roadmap → Bus-voltage detector.
