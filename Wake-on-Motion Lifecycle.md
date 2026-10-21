Hardware / firmware — power state design

# Power lifecycle of a motorcycle GPS tracker

There's no battery of its own here — every microamp is pulled straight from the bike's starter battery. So the logic below isn't just "sleep vs. awake": every single wake reads the bus voltage first, and that one number decides whether it's safe to take a GPS fix at all, or whether the tracker needs to cut its own power to protect the bike's ability to start. Below the flowchart: the exact MCU/GPS/modem mode behind each box, every rule as a table, and what has to survive in flash between sleeps.

- hardwired direct to the motorcycle battery — no backup cell
- accelerometer wake-on-motion
- heartbeat fix every 6h (OK) / 12h (LOW)
- bus voltage doubles as the engine-running sensor
- remote "track me now" command jumps straight to pursuit rate

## Wake, decide, act

Every wake — accelerometer, timer, or an incoming remote command — runs the same sequence: read the bus voltage first, because it's both the only power source and the only signal for what the bike is doing. Round dashed circles marked **SLEEP** all lead to the same place, **STANDBY_SLEEP**, drawn as jump connectors instead of long wires. The dashed rectangle marked **REBOOT** is different on purpose: **SELF_DISCONNECT** actually powers the MCU off, so recovering from it is a cold boot, not a loop-back — it re-enters at **BOOT_INIT** at the very top instead of dropping back into the sleep loop directly. The same dashed-jump convention shows up twice more, in two more colors: violet boxes marked **PURSUIT_START** are every path that can trigger a "track me now" burst — a remote command can arrive while the tracker is asleep, mid-watch, or already pursuing — and green boxes marked **TRACK_ACTIVE** are pursuit or watch handing off to ordinary active tracking the moment the engine starts charging again.

accelerometer-decided charging / recovered moving, engine off critical voltage remote command

Voltage tier (**OK ≥ 12.0V** / **LOW 11.5–12.0V** / **CRITICAL \< 11.5V**) sets the numbers inside each box — OK and LOW run the identical flow at different intervals; CRITICAL is the only tier that changes the flow's shape, by cutting the load. Reconnection at **SELF_DISCONNECT** is pure analog hardware (a comparator with hysteresis driving the load switch), not a firmware polling loop — that's the only way to honestly claim ≈0 mA while disconnected. Any successful report — heartbeat, ride, or pursuit — resets the heartbeat clock, so a bike that's actually being ridden never fires a redundant heartbeat fix on top of it.

The two diamonds inside **PURSUIT** and **WATCH** each check three things in a fixed order — idle first, then charging, then the timing condition — so treat their three exits as an if/elif/else chain, not three independent checks. **PURSUIT** is deliberately indifferent to the OK/LOW battery split: once triggered, either by real theft motion or by a remote command, it reports every 10–15s regardless of tier, on the reasoning that recovering the bike matters more than a few extra days of standby life. It's still hard-capped by **SELF_DISCONNECT**'s independent hardware comparator, so an aggressive pursuit can't itself brick the bike's ability to start — worst case it just triggers the disconnect sooner than usual. A **WATCH → PURSUIT** escalation and a fresh **STANDBY_SLEEP → PURSUIT_START** command both do the same thing: they're drawn as dashed jumps rather than two long wires converging on one box.

## Hardware modes, state by state

Three power modes underlie all of this: **ACTIVE** (MCU running, doing work), **DEEP_SLEEP** (MCU in retention, RAM intact, woken by the accelerometer interrupt, an RTC alarm, or an inbound command — this is what STANDBY_SLEEP actually is), and **HIBERNATE** (the MCU's own supply rail is switched off — this is what SELF_DISCONNECT actually is, and the only way out is a hardware reboot). Every box in the flowchart above is one of the three, paired with a GPS and modem submode.

One tradeoff worth naming: true PSM (Power Save Mode) drops off the paging channel entirely, so nothing can reach the device until *it* wakes up and checks in — that's incompatible with an on-demand pursuit command. **IDLE (paging)** below means "registered and periodically listening," which costs more than PSM but is what makes the modem's RING/interrupt pin usable as a real wake source. If your module truly can't stay reachable that cheaply, the fallback is polling for a pending command at every timer wake instead — same idea as the heartbeat check, just laggier.

| State | MCU mode | GPS mode | Modem mode | Draw |
| --- | --- | --- | --- | --- |
| STANDBY_SLEEP | DEEP_SLEEP (retention) | BACKUP — RTC + ephemeris kept alive on the module's own backup pin, main GPS rail off | IDLE (paging) — registered, listening for RING/SMS | ≈1.5 mA |
| READ_VOLTAGE / LOG_ONLY | ACTIVE (brief, \<50ms) | BACKUP (unchanged) | IDLE (unchanged) | ≈2 mA |
| HEARTBEAT | ACTIVE | ACQUIRING (hot-start, ≤10s budget) → BACKUP after | wakes to TX one packet, back to IDLE | ≈28–35 mA, \~150 mA pulse |
| GPS_ACQUIRE | ACTIVE | ACQUIRING — hot or cold start | IDLE (not transmitting yet) | ≈28–35 mA |
| TRACK_ACTIVE | ACTIVE | NAV — continuous fix | wakes to TX every 30–60s (OK) / 2–5min (LOW), IDLE between | ≈30 mA avg, \~150 mA pulses |
| PURSUIT_START / PURSUIT | ACTIVE | NAV — continuous fix, no power-saving duty cycle | wakes to TX every 10–15s, IDLE between | ≈32 mA avg, \~150 mA pulses |
| WATCH | ACTIVE, briefly, every 3–5min | ACQUIRING, then BACKUP between retries | wakes to TX, back to IDLE (still listening for a command) | ≈150 mA pulse |
| SELF_DISCONNECT (HIBERNATE) | fully OFF — own rail cut | fully OFF — ephemeris lost after ≈4h | fully OFF — drops network registration entirely, no RING possible | ≈0 mA + ≈1 µA comparator |

## The rules, as rules

The same logic as the flowchart, read top to bottom as a rule table instead of a diagram — useful for turning directly into a switch statement.

| When | Does | Then |
| --- | --- | --- |
| Any wake, first thing | READ_VOLTAGE — sample bus, \<50ms | branch on the reading below |
| Bus \< 11.5V | send a low-battery alert if the link is still up, then open the MOSFET load switch | MCU powers off (HIBERNATE); an analog comparator takes over watching the bus |
| While disconnected, bus ≥ 12.2V or charging (≥13.2V) detected | comparator closes the load switch — pure hardware, no firmware polling | MCU reboots → BOOT_INIT |
| BOOT_INIT, persisted last_state == SELF_DISCONNECT | SEND_RECONNECT_ALERT with last known fix + downtime duration | clear the flag, join STANDBY_SLEEP |
| BOOT_INIT, normal boot | — | join STANDBY_SLEEP directly |
| Bus OK, woke on timer, heartbeat due (≥6h OK / ≥12h LOW since last report) | HEARTBEAT — hot-start attempt ≤10s, send status (position or "no fix" + voltage) | reset the heartbeat clock, → STANDBY_SLEEP |
| Bus OK, woke on timer, heartbeat not due | LOG_ONLY — store the voltage sample | → STANDBY_SLEEP, next check per voltage tier |
| Bus OK, woke on motion, not sustained ≥0.3g/200ms | discard as spurious | → STANDBY_SLEEP, unchanged |
| Bus OK, woke on motion, sustained ≥0.3g/200ms | GPS_ACQUIRE — hot-start ≤8s if ephemeris \<4h old, else cold-start ≤45s | proceed to the charging check |
| Fix acquired, engine charging (≥13.2V) | TRACK_ACTIVE — continuous fix, report every 30–60s (OK) / 2–5min (LOW) | stays active until idle ≥5 min, then final fix → STANDBY_SLEEP |
| Fix acquired, engine not charging, still moving | PURSUIT_START — reset a 10-minute window, hot-start fix | enters PURSUIT: continuous fix, report every 10–15s |
| Bus OK, woke on an inbound remote command (modem RING), from any state | PURSUIT_START — same as above, immediately, regardless of motion or charging | enters or renews PURSUIT for another 10 minutes |
| In PURSUIT, checked every 10–15s report cycle, in this order | 1) idle ≥2 min → final report. 2) else charging → none. 3) else window elapsed → none. | 1) → STANDBY_SLEEP. 2) → TRACK_ACTIVE. 3) → WATCH. Otherwise, loop. |
| In WATCH, checked every 3–5 min retry cycle, in this order | 1) idle ≥5 min → final report. 2) else charging → none. 3) else new remote command → none. | 1) → STANDBY_SLEEP, shortened next interval. 2) → TRACK_ACTIVE. 3) → PURSUIT_START (re-escalate). Otherwise, loop. |

PURSUIT and WATCH both ignore the OK/LOW battery-tier throttle on purpose — they run at their own fixed rate regardless of voltage tier, right up until **SELF_DISCONNECT**'s independent hardware comparator cuts power at 11.5V. That's the one rule in this table with no software override.

## What has to survive between sleeps

DEEP_SLEEP keeps RAM intact, so most in-progress state (like a partial debounce count) just sits there for free. HIBERNATE doesn't — the MCU's own supply is cut — so anything BOOT_INIT needs afterward has to already be in non-volatile storage before that happens.

| Field | Type | Survives | Why |
| --- | --- | --- | --- |
| last_state | enum, 1 byte | HIBERNATE | the one flag BOOT_INIT reads to decide whether to send a reconnect alert |
| last_fix_lat / lon | int32 ×2 (1e-7°) | HIBERNATE | sent on reconnect or as a heartbeat fallback when a fresh fix fails |
| last_fix_time | uint32, unix time | HIBERNATE | lets a report say how stale the position is |
| last_report_time | uint32, unix time | HIBERNATE | drives the heartbeat-due check — any successful report of any kind resets it |
| disconnect_time | uint32, unix time | HIBERNATE | timestamped when SELF_DISCONNECT trips, used to compute downtime for the reconnect alert |
| battery_tier | enum, 1 byte | HIBERNATE | avoids re-deriving OK/LOW/CRITICAL before the first fresh voltage read completes |
| gps_ephemeris | opaque blob, module-managed | DEEP_SLEEP only (its own backup pin) | the whole reason hot-start exists — lost automatically once HIBERNATE cuts everything |
| pending_points\[\] | ring buffer, e.g. 64 × {lat, lon, time} | HIBERNATE | fixes queued for the next successful upload, so a dropped link doesn't lose points |
| pursuit_window_end | uint32, unix time | DEEP_SLEEP only | when the current 10-minute PURSUIT burst expires; a fresh command just pushes this forward instead of restarting a separate counter |
| last_command_id | uint32 or hash | DEEP_SLEEP only | dedupes a re-delivered or retried remote command so it doesn't needlessly re-arm the window twice |
| motion_debounce | small struct, a few bytes | DEEP_SLEEP only | in-progress sustained-motion counters — irrelevant once HIBERNATE has fully reset the system anyway |

Writing plain flash on every wake will wear it out over a few years at this duty cycle. Put the fields that change every wake or every few minutes (**last_report_time**, **disconnect_time**, **battery_tier**) in FRAM/MRAM or a battery-backed RTC register domain if the MCU has one, and reserve actual flash writes for things that change rarely, like **pending_points** batches.

## Power budget

There's no backup cell to size a runtime for — the only budget that matters is how much charge the tracker pulls out of the bike's own battery while it's sitting parked.

| State | Draw | Duration | Notes |
| --- | --- | --- | --- |
| STANDBY_SLEEP | ≈1.5 mA | most of parked time | MCU deep sleep, accelerometer armed. Costs more than a bare-minimum sleep because the modem stays in an IDLE/paging mode, not full PSM, so a remote pursuit command can actually reach it. A periodic timer also wakes it every \~45 min even with zero motion, just to sample voltage. |
| READ_VOLTAGE | ≈2 mA | \< 50 ms | Runs on every single wake, before anything else — every downstream decision is based on this one reading. |
| GPS_ACQUIRE (hot) | ≈28 mA | 2–8 s | Valid ephemeris cached from under 4 hours ago. |
| GPS_ACQUIRE (cold) | ≈35 mA | up to 45 s | No valid almanac — worth avoiding by keeping GPS in backup mode between fixes when the budget allows. |
| TRACK_ACTIVE + report | ≈30 mA avg, ≈150 mA pulses | while riding | GPS on continuously; the transmit pulse every 30–60 s (or 2–5 min on LOW battery) is the single biggest line item in the whole budget. |
| PURSUIT | ≈32 mA avg, ≈150 mA pulses | 10-min bursts, renewable | Fires on confirmed theft motion or a remote command. Deliberately ignores the LOW-battery throttle — reports every 10–15s regardless of tier — because getting the bike back matters more than a few extra days of standby. |
| WATCH | ≈150 mA pulse | one fix + alert, then 3–5 min retries | What PURSUIT downgrades to once its 10-minute window runs out and the bike is still moving without charging — no longer time-capped, just slower, until it stops, the engine starts, or another command re-escalates it. |
| SELF_DISCONNECT | ≈0 mA (tracker) | until voltage recovers | Own MOSFET load switch open. An independent µA-class voltage supervisor keeps watching so the MCU itself doesn't have to stay powered to check. |

720mAh/day

Always-on tracking

GPS + modem never sleep — ≈9% of an 8 Ah battery, every day

70mAh/day

Accelerometer-gated

parked ≈95% of the time, modem kept reachable

≈10× less draw

For scale: a typical 8 Ah motorcycle battery self-discharges roughly 35–165 mAh/day on its own (lead-acid, ≈1–5% per month) — a well-gated, reachable tracker lands inside that same range rather than clearly on top of it. An ungated one roughly doubles or triples the total, and can leave a bike parked for two weeks unable to start. A live pursuit burst adds only a few extra mAh even at 10 minutes of continuous reporting — it's the rare, short exception, not a daily cost. Either way, the numbers are estimates: it's **SELF_DISCONNECT at 11.5V** that actually guarantees the bike can still crank, not the average draw.

## Worth building in from day one

None of these change the shape of the flowchart above, but they're the difference between it working on paper and working after three weeks in a garage.

### Debounce the accelerometer

Don't wake the modem on a single spike — require motion above threshold for a sustained window (0.3 g for 200 ms), so wind rock or someone bumping the parked bike doesn't burn a GPS fix.

### Cache ephemeris for hot starts

Keep the GPS module in a low-power backup state instead of fully off when you can spare the µA. A hot start (2–8 s) beats a cold start (up to 45 s) on every single wake.

### Batch, then batch again

Store fixes locally and upload in bursts. The radio transmit pulse is usually the biggest single line item in the whole power budget — bigger than the GPS.

### Give voltage-sensing its own supervisor

Do the critical-voltage check — and the reconnect check while disconnected — on a tiny always-on comparator IC that draws nanoamps, not the main MCU. Otherwise "checking if it's safe to stay on" becomes its own drain.

### Don't trip on cranking dips

Starting the engine can sag the bus to 6–9 V for a fraction of a second. Debounce the critical-voltage read (low for >2 s) so a normal crank doesn't get misread as a dying battery and self-disconnect right as the rider needs it least.

### Add hysteresis to every threshold

Use separate rising/falling voltage points for OK↔LOW and disconnect↔reconnect (disconnect at 11.5 V, reconnect only above 12.2 V or on charging). Without a gap, a battery sitting right at the line will chatter the tracker on and off.

### Pick a real channel for the pursuit command

SMS-triggered wake is the simplest: most cellular modules can interrupt the MCU on an inbound SMS from an idle-registered state without a data session. An app-driven push needs the modem to stay attached with short-cycle paging (or LoRaWAN Class C, which listens continuously); either costs more standby current than the SMS route to buy the same instant reachability.

Numbers throughout are typical orders of magnitude for a u-blox-class GPS module and an NB-IoT/LTE-M or LoRa radio — treat them as a starting point for your own current-draw measurements, not a datasheet.