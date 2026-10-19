# Sensor Test Plan — QM Series Photoelectric Sensors

Covers the 8 QM-series photo eyes (2 gates × 4 beams) read by
[`firmware/src/sensors/SensorEvents.cpp`](../firmware/src/sensors/SensorEvents.cpp)
on the BeaglePlay through libgpiod, and how their output feeds the race state machine.

Work through the phases in order. Each one assumes the one before it passed.
Log results in the [Results log](#results-log) at the bottom.

---

## 0. Configuration (fill in before testing)

| Item | Value | Notes |
|---|---|---|
| Exact part number | `QM____-0_-0_` | `-0N-` = NPN, `-0P-` = PNP. **Firmware assumes NPN.** |
| Sensor type | through-beam / retroreflective / diffuse | Sets which curves and spacing rules apply |
| Connection | 2 m cable (`-0A`) / M8 (`-0F`) | |
| Supply voltage (L+) | ___ VDC | Must be 10–30 VDC, ripple ≤ 10% |
| Light-on / Dark-on switch | ___ | Firmware assumes the output **conducts when the beam is blocked** |
| Reflector (if retro) | RL___ | |
| GPIO chip | `/dev/gpiochip_` | `CHIP_PATH` in code is `/dev/gpiochip2` |
| Pull-up | internal / external ___ kΩ to **3.3 V** | |

Datasheet numbers this plan uses:

- Operating voltage 10–30 VDC; no-load current 30 mA (red) / 45 mA (IR) per device
- Output: ≤ 100 mA load, ≤ 2 V drop, ≤ 10 µA leakage
- Switching frequency 1 kHz (QMRB/QMR8/QMI9, background suppression) or 2 kHz (QMRx7, retro, through-beam). That gives roughly 0.25–0.5 ms response.
- Time delay before availability ≤ 100 ms after power-up
- Wiring (NPN, Diagram 1): 1 Brown = L+, 3 Blue = L−, 4 Black = output. Through-beam emitter (Diagram 3): 2 White not connected.

---

## 1. Electrical safety (do this before connecting anything to the BeaglePlay)

The BeaglePlay GPIO is **3.3 V only**. The sensors run at 10–30 V. Getting this wrong will damage the board.

| # | Test | Pass criteria |
|---|---|---|
| 1.1 | Confirm the part number on each sensor says `-0N-` (NPN) | All 8 (16 for through-beam) are NPN. A PNP output drives L+ onto the GPIO pin. |
| 1.2 | Power one sensor from the bench supply with **no** GPIO connected. Put a 4.7 kΩ pull-up from Black to 3.3 V and measure Black→GND with the beam clear and with it blocked | Both readings are between 0 and 3.3 V, and the low reading is < 0.8 V |
| 1.3 | Confirm the pull-up goes to **3.3 V, never to L+** | Verified on schematic and by measurement |
| 1.4 | Confirm the sensor supply L− and BeaglePlay GND share a common ground | Continuity confirmed |
| 1.5 | Power budget: count the devices (×2 for through-beam) × 30/45 mA + margin | Supply rating ≥ 1.5 × total. Example: 16 IR devices × 45 mA = 720 mA, so use ≥ 1.1 A. |
| 1.6 | Measure supply ripple under full load on a scope | ≤ 10% of L+ |

---

## 2. Single-sensor bench check (one sensor at a time)

| # | Test | Pass criteria |
|---|---|---|
| 2.1 | Power up and observe the LEDs | The green (excess gain/power) LED is on. The yellow LED changes when the beam is blocked. |
| 2.2 | Find out which state the output conducts in: block the beam, measure Black | Low (≈ 0 V) when **blocked**. If it is low when clear, flip the L-on/D-on switch and record the new setting in §0. |
| 2.3 | Set sensitivity (the 270° pot, or 4-turn on background suppression) at the real mounting distance | The yellow LED switches reliably when the target crosses. The green excess-gain LED stays on when clear (good margin). |
| 2.4 | Measure the max reliable distance at the real mounting geometry | Comfortably inside the rated range (aim for excess gain > 2× at operating distance; see the characteristic curves) |
| 2.5 | Power-cycle and watch the output for the first 100 ms on a scope | Record any glitch. Firmware must ignore edges during this window (see §7). |

Repeat for all sensors and label each one with its sensor index (0–7).

---

## 3. GPIO bring-up on the BeaglePlay

| # | Test | How | Pass criteria |
|---|---|---|---|
| 3.1 | Find the correct chip and line offsets | `gpioinfo`, and map each header pin you wired | The table below is filled in and matches `SENSORS[]` in code (currently a TODO, lines 7–14 on chip 2) |
| 3.2 | Raw edges per line | `gpiomon -c gpiochip2 -b pull-up <line>` and block each beam in turn | Exactly one falling edge on block and one rising edge on clear, on the **expected** line only |
| 3.3 | Internal pull-up actually applies | Measure the pin voltage with the sensor unplugged while the program holds the line | ≈ 3.3 V. If it floats, the pinmux is ignoring bias, so add external pull-ups. |
| 3.4 | Run the firmware sensor program and block each beam | Console shows `INCOMING/OUTGOING entered … (sensor N)` | Correct gate and correct sensor number (1–4) for every physical sensor |

**Line map** (fill in):

| Index | Gate | Bit | Header pin | gpiochip | Line | Verified |
|---|---|---|---|---|---|---|
| 0 | INCOMING | 0 | | | | ☐ |
| 1 | INCOMING | 1 | | | | ☐ |
| 2 | INCOMING | 2 | | | | ☐ |
| 3 | INCOMING | 3 | | | | ☐ |
| 4 | OUTGOING | 0 | | | | ☐ |
| 5 | OUTGOING | 1 | | | | ☐ |
| 6 | OUTGOING | 2 | | | | ☐ |
| 7 | OUTGOING | 3 | | | | ☐ |

---

## 4. Signal quality and debounce

The code configures no debounce (`line_settings` has no `set_debounce_period`), so any chatter becomes extra events.

| # | Test | Pass criteria |
|---|---|---|
| 4.1 | Scope the output while slowly passing a target in and out of the beam edge | Single clean transition with no chatter. If there is chatter, record its width. |
| 4.2 | Scope with the full 2 m cable (plus any extension) beside motors, lights and power cabling | No induced glitches > 1 V on the GPIO pin |
| 4.3 | Edge-count test: block and clear one beam 100 times by hand | `gpiomon` shows exactly 100 falling and 100 rising edges |
| 4.4 | Decide on debounce | If chatter exists, set `set_debounce_period` to a value below the shortest real blocked time (see 6.4) and re-run 4.3 |

---

## 5. Timing accuracy

Goal: confirm the timestamps are accurate enough for race timing. **Target: ±1 ms** (team to confirm the required resolution).

### 5a. Electrical reference (removes the sensor from the measurement)

Use a microcontroller or function generator to drive two GPIO inputs through an open-drain/NPN transistor, with a known offset between them.

| # | Test | Pass criteria |
|---|---|---|
| 5.1 | Inject pulses on an INCOMING line and an OUTGOING line with a known offset (1 ms, 10 ms, 100 ms, 1 s) | The `PAIR` delta matches within ±0.1 ms over 50 trials |
| 5.2 | Repeat 5.1 while the board is under CPU load (`stress-ng --cpu 4`) and the GUI is running | Same accuracy. Kernel timestamps should not be affected by load. |
| 5.3 | Confirm the gpiod event clock matches `std::chrono::steady_clock` (the state machine relies on this) | Print both clocks at an event; they differ by < 1 ms |

### 5b. Physical reference (includes the sensor response time)

| # | Test | Pass criteria |
|---|---|---|
| 5.4 | Drop a rod of known length through a beam, or use a rotating wheel with a flag at a known RPM | Measured blocked time matches the calculated value within sensor response (≤ 0.5 ms) + 5.1 error |
| 5.5 | Pass a target through gate A then gate B at a measured spacing and speed | Delta is consistent across 20 runs (std dev < 1 ms) |
| 5.6 | Event burst: block all 8 beams at once (a board across both gates) | No events lost (the edge buffer is 64). All 8 enter events are logged. |

---

## 6. Gate logic (software, no hardware needed)

`handle()` and `try_pair()` should be unit tested by feeding synthetic `SensorEvent`s. **Prerequisite:** move the gate logic out of `SensorEvents.cpp`'s `main()` into a testable unit. Right now `SensorEvents.cpp` and `main.cpp` both define `main()`, so the `firmware` target will not link.

| # | Scenario (input events) | Expected |
|---|---|---|
| 6.1 | One beam blocks then clears | 1 `entered` at the block time, 1 `cleared`, blocked time correct |
| 6.2 | Beams 0→1→2→3 block in sequence, then all clear | One `entered` (time of the first beam), one `cleared` (time of the last beam) |
| 6.3 | Gate clears and re-blocks within 50 ms (`REARM_NS`) | Treated as the same crossing, so no second `entered` |
| 6.4 | Gate clears and re-blocks after 50 ms | A new `entered` event |
| 6.5 | INCOMING enters, then OUTGOING enters | One `PAIR` with a positive delta |
| 6.6 | OUTGOING enters first, then INCOMING | `PAIR` with a negative delta and an "outgoing crossed FIRST" message |
| 6.7 | Two INCOMING entries before any OUTGOING | Define the expected behavior. Currently the second entry silently overwrites the first `pending_in`. |
| 6.8 | Clear event without a prior block (e.g. after a restart while blocked) | No crash and no bogus negative blocked time |
| 6.9 | A sensor stuck blocked (mask never returns to 0) | Define the behavior. Currently the gate can never fire again, so a fault should be detected. |

> 6.3 and 6.4: verify that 50 ms is right. It must be **longer** than the gaps between legs/tail on a real dog and **shorter** than the time between two different dogs crossing. Measure this in §7.

---

## 7. Field / environment tests (real mounting, real conditions)

| # | Test | Pass criteria |
|---|---|---|
| 7.1 | **Crosstalk:** with all 8 sensors running, block one beam at a time | Only that sensor's line changes. Adjacent emitters must not trigger neighboring receivers. Retro sensors facing each other across lanes must not interfere. |
| 7.2 | **Ambient light:** test under the actual venue lighting, plus a flashlight, phone flash and sunlight on the receiver | No false triggers, and the green excess-gain LED stays on |
| 7.3 | **Target variety:** white, grey, black and fluffy/long-hair targets (or real dogs) at fast and slow speeds | Every crossing detected. (Diffuse models lose a lot of range on dark targets; see the grey 18% curves.) |
| 7.4 | **Smallest target:** the smallest thing that must be detected (small dog leg, tail) | Detected at all beam heights |
| 7.5 | **Dog gaps:** record raw edges as real dogs run through | Note the longest gap within a single dog. Use it to set `REARM_NS` (6.3). |
| 7.6 | **Misalignment:** bump the stands and nudge the reflector ±15° | Still detects, or the alignment indicator shows the fault before a race |
| 7.7 | **Vibration:** stomp near the stands and run dogs on the mat | No false triggers |
| 7.8 | **Dust/dirt** on the lens or reflector (the datasheet notes up to 90% range loss) | Excess gain margin still sufficient. Add a cleaning step to setup. |
| 7.9 | **Power-up:** start the system with beams blocked and with beams clear | No phantom crossings in the first 100 ms. Gate state matches reality. |
| 7.10 | **Temperature:** if outdoors, run at the hottest/coldest expected conditions (rated −25 to 70 °C) | Behavior unchanged |

---

## 8. Integration with the race state machine

Run once `poll_inputs()` is wired to the sensor queue.

| # | Test | Pass criteria |
|---|---|---|
| 8.1 | Dog staged → `lane_aligned` true → alignment light on | Correct lane's light |
| 8.2 | Break the beam during `Sequence` | `FalseStart` on the correct lane, red light for 2 s |
| 8.3 | Break the beam just **after** green | Not a false start. Time recorded. |
| 8.4 | Both lanes finish | Winner correct, times match the sensor timestamps (not `now()`; see the TODO in `Running`) |
| 8.5 | Judge abort mid-race | Gate masks and `pending_in/out` cleared (currently a TODO in `Reset`) |
| 8.6 | `poll_inputs()` never blocks | State machine tick stays at ~1 ms with no sensor activity (`EventQueue::pop()` blocks, so a non-blocking `try_pop()` is required) |

---

## 9. Endurance

| # | Test | Pass criteria |
|---|---|---|
| 9.1 | Run 8 h idle | Zero false events |
| 9.2 | 1,000 automated crossings (wheel/flag rig) | 1,000 detected, 0 extra, no memory growth |
| 9.3 | Unplug and replug a sensor while running | Program keeps running and the fault is visible to the operator |

---

## Open questions for the team

1. **Gate ↔ lane mapping.** The sensor code has 2 gates (`INCOMING`/`OUTGOING`). The state machine expects per-lane `LEFT`/`RIGHT` signals, and the old Python script used `LEFT_ENTER/EXIT` and `RIGHT_ENTER/EXIT`. Which physical layout is final?
2. What timing resolution do the rules require (0.01 s? 0.001 s)? This sets the §5 pass criteria.
3. Which sensor type and model was bought? This determines spacing, reflectors and crosstalk risk.
4. How should a stuck or disconnected sensor be reported to the judge/GUI?

---

## Results log

| Date | Test # | Sensor(s) | Result (P/F) | Measured | Tester | Notes |
|---|---|---|---|---|---|---|
| | | | | | | |
