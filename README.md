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

Add a low-power accelerometer with a hardware wake-up interrupt, such as the ST LIS2DH12 or LIS3DH (I²C, a few µA in
low-power mode, configurable motion threshold and duration). Wire its INT1 to an RTC-capable GPIO so it can wake the
ESP32 from deep sleep (`ext1`). The firmware already has the `MotionSensor` interface; adding the sensor means writing a
driver and turning on motion-gated `GPS_ACQUIRE` and the idle timers.

### Bus-voltage detector and self-disconnect (recommendation)

The lifecycle doc needs two separate things. Keep them separate in hardware:

**1. Firmware measurement (tiers, `batV`, charging detection)**

- Divider from the switched 12 V bus to an **ADC1** pin. ADC2 is unusable while the radio is active. GPIO34 and GPIO35
  are input-only ADC1 pins; check against the T-Call V1.0 schematic that the one you pick is free.
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
