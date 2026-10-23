# CLAUDE.md

Firmware for a motorcycle GPS tracker: LilyGO T-Call A7670 V1.0 (ESP32 + SIMCom A7670E-FASE LTE Cat-1 modem with built-in
GNSS), Arduino on PlatformIO. It reports to the AWS IoT backend in `../gps_tracker_backend`.

## Sources of truth, in priority order

1. `../gps_tracker_backend/PROTOCOL.md`: wire protocol v1 (MQTT topics, JSON shapes, validation). Wins every conflict.
2. `docs/ARCHITECTURE.md`: module map, state machine, persistence, delivery phases and the **decisions log (D1–D9)**.
   Read it before changing structure, and add a decision row when a non-obvious choice is made.
3. `Wake-on-Motion Lifecycle.md`: power/state behaviour draft, lower priority than the protocol.
4. `README.md`: setup, provisioning, tests, hardware roadmap, future improvements.

## Commands

`pio` lives in `~/.platformio/penv/bin/pio` (not on PATH by default).

```sh
pio run -e T-Call-A7670X-V1-0-bridge                    # build firmware (default env)
pio run -e T-Call-A7670X-V1-0-bridge -t upload          # flash: user presses BOOT+RST first, RST after
pio run -e T-Call-A7670X-V1-0-bridge -t erase           # works through the bridge thanks to scripts/bridge_no_reset.py
pio run -e T-Call-A7670X-V1-0-bridge-provision -t upload  # one-off: writes AWS certs into the modem
pio device monitor                                       # 115200, through the ESP32-C3 USB-UART bridge
# Unit tests (lib/core). This Mac's Command Line Tools are damaged, so add the SDK libc++ path:
PLATFORMIO_BUILD_FLAGS="-isystem $(xcrun --show-sdk-path)/usr/include/c++/v1" pio test -e native
```

- The first `pio test` after a build-flag change sometimes errors on `unity_config.c` (a PlatformIO race). Run it again.
- Flashing needs the user's hands (BOOT+RST). Ask the user to do it, then read the log they paste.
- `secrets.ini` (gitignored) holds `device_id`, `mqtt_host`, `apn` and `certs_dir`. **A permission rule blocks
  reading/writing `secrets*` files; don't work around it.** The template is in README → Setup. `mqtt_host` must be
  the bare host, with no `:8883`.

## Code layout and rules

```
lib/core/      pure C++17, no Arduino: protocol encoding, parsers, policies. Unit-tested on the laptop.
src/platform/  Board (only file including utilities.h), Log, Watchdog
src/drivers/   AtChannel (sole owner of the modem UART), A7670Modem, gnss/ModemGnss
src/services/  NetworkLink, ClockService, MqttSession, Uplink, PositionService
src/app/       Lifecycle (link state machine), Tracking (GNSS → outbox → telemetry/heartbeat)
src/main.cpp   wiring only
src/provision/ provisioning firmware (embeds the private key via scripts/embed_certs.py: erase flash after use)
```

- A layer calls only the layers below it. Keep logic that can be pure in `lib/core`, with a test in `test/test_<name>/`.
- **Never use TinyGSM** (D6). Its `waitResponse` drops unsolicited lines. Every modem line goes through `AtChannel`, and
  unsolicited results reach `UrcListener`s. Lines starting with `$` (NMEA) are always unsolicited and never traced.
- **Every AT wait must outlast the modem's own timeout**, so the modem answers before we give up. After a timeout,
  `AtChannel` swallows the late result. Use `probe()` for "is the modem on?" checks.
- Single cooperative loop, no FreeRTOS tasks. Every blocking wait feeds the 30 s watchdog (`AtChannel` loops do).
- Logging: `LOGI/LOGW/LOGE(TAG, ...)`, one tag per module. Never log key material or certificate bytes.
- Time: wire and memory use UTC epoch **ms**; stored fixes use **seconds** (D5). The clock stays unset until NTP or
  GNSS sets it; nothing is published while it is unset.
- JSON: ArduinoJson 7.4.3 (D7). The encoder never returns a truncated payload; telemetry batches shrink until they fit
  the modem's 10,240-byte publish limit.
- Style: match the existing code's comment density (short "why" comments), 4-space indent, ~120 columns.

## Hardware quirks learned on the device (A7670E-FASE, firmware A011B07A7670M7_F)

- `AT+CMQTTCONNECT` holds its `OK` until the TCP+TLS handshake is done (~6 s), so the command needs the full 60 s.
- `AT+CGNSSINFO` answers are cut at **98 characters**. That's why NMEA is the main GNSS source (D9).
- `AT+CNTP` sometimes returns `+CNTP: 6` (failure) for a while, then succeeds. Hence the fallback server list.
- Registration shows `+CEREG: 0,5` (roaming), which is fine. Signal on the bench is about -51 dBm.
- Cold GNSS start by a window took about 10 min. Window multipath gave ~90 m jumps and up to 26 km/h of speed at
  HDOP 1.6–2.1, so HDOP alone is not a trustworthy quality signal.
- The ESP32-C3 bridge resets itself when esptool toggles DTR/RTS. All esptool calls need `--before/--after no_reset`.

## Status (2026-10-22)

- **P1 connected tracker: done and verified.** `boot` reaches DynamoDB (PUBACK in ~1 s, about 9 s from power-on).
- **P2 always-on tracking: implemented, partly verified.**
  - Verified on hardware: GNSS lock, the first fix sent immediately, 60 s telemetry batches with PUBACK, the outbox
    draining to 0, and the clock resynced from GNSS.
  - Store-and-forward (link down, then backlog drain) is **not yet tested on hardware**.
- **Latest change: implemented and unit-tested (71 tests pass), not yet run on the device.**
  - NMEA (`RMC`/`GGA`/`GST`) replaces `CGNSSINFO` as the GNSS source; `CGNSSINFO` remains as a fallback.
  - `FixQuality` rates fixes good/fair/poor (25 m / 100 m) from the GST estimate, with an HDOP fallback capped at fair.
    Poor fixes are not sent; speed and course are kept only for good fixes; fixes carry `acc` (metres).
  - `MotionDetector` decides moving/stationary from distance (150 m from the parking anchor), replacing the old
    speed-only `StationaryDetector`.
  - NTP falls back across `au.pool.ntp.org`, `time.google.com` and `pool.ntp.org`, and retries the clock step
    without going back through the network step.
  - Other: `AT+CAGPS` once per boot, `CGNSSMODE=4`, "lock lost" only after 3 empty readings, and a restart of a
    silent receiver.
  - None of this latest work is committed yet (the user commits themselves; ask before committing).

## Next steps (the plan)

1. **The user runs the latest firmware by the window and pastes the log.** Check:
   - `GNSS: +CGNSSMODE: 4`, and whether `CGNSSNMEA` / `CGNSSTST` / `CGNSSPORTSWITCH` were refused;
   - `GNSS: receiver reports its own error estimate (GST)`, the key unknown. If it's missing, quality falls back to
     HDOP (fair at best) and motion detection relies on distance alone;
   - `GNSS: no NMEA output; polling AT+CGNSSINFO instead`, meaning NMEA didn't start;
   - `GNSS: assisted GPS data loaded…` and the time to first lock after it;
   - `POS: quality … (~N m, receiver estimate|hdop guess)`, and that `TRACK:` stays **stationary** by the window.
   - Adjust `TrackerConfig` thresholds (lib/core/src/Config.h) from real numbers if needed.
2. Field test for P2: ride or walk around. Check moving→stationary transitions, flush on stop, and the heartbeat after
   15 min parked. Store-and-forward test: unplug the LTE antenna mid-ride, reconnect, and confirm the backlog drains
   oldest first and the gap is filled in DynamoDB.
3. Ask the backend owner to add the optional `acc` (fix error, metres) and `rst` (boot reset reason) fields to
   `PROTOCOL.md`.
4. **P3 sleep lifecycle:** `LifecyclePolicy` as a pure function, ESP32 deep sleep with RTC-memory persistence (outbox
   and clock anchor; RAM/PSRAM are lost in deep sleep), the timer heartbeat, and modem DTR sleep (`AT+CSCLK=1`).
   Re-sync the clock on wake.
5. **P4 remote wake:** RING pin (GPIO13, ext0) for SMS and calls. Allowlisted numbers plus a PIN for `TRACK <pin>` /
   `STOP <pin>`. Delete every SMS after reading (`AT+CMGL`/`CMGD`). `CommandService` → PURSUIT/WATCH. Parse `cmd`
   JSON with ArduinoJson.
6. **P5 power protection** (needs hardware): bus-voltage ADC divider, `PowerPolicy` tiers, `low_battery` / `shutdown`,
   and a load switch. See README → Hardware roadmap.
7. Future (README): heartbeat 15 min → 6 h / 12 h (needs a protocol change), and an accelerometer in the next
   hardware version, replacing GPS-based motion detection.

Smaller open items:
- Make the `AT+CMQTTDISC` / `CMQTTREL` / `CMQTTSTOP` clean-up warnings at connect quieter (they're expected on a
  fresh start).
- Turn `AT_TRACE` off by default once bring-up is over.
