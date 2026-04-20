# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

ESP32-based scoreboard for a VEX robot football arena. Detects goals via two VL53L0X time-of-flight sensors, drives a 2.8" ILI9341 TFT, and plays sounds through a PWM speaker. The repo root (one level up) has a detailed `README.md` with full wiring tables and a state diagram — read it before making hardware-related changes.

## Build / Flash

PlatformIO project, target `esp32dev`:

```bash
pio run                 # compile
pio run -t upload       # compile + flash
pio device monitor      # 115200 baud serial console
pio run -t clean        # wipe build artefacts
```

There are no unit tests; `test/` is an empty PlatformIO scaffold.

## Code Layout

The entire firmware lives in a single file: `src/vex-football.ino`. It is organized top-to-bottom in labelled banner sections:

1. **CONFIGURABLE SETTINGS** — team names/colours, half duration, goal-detection thresholds and timings. Tune these before touching logic.
2. **PIN DEFINITIONS** — TFT, two I2C buses (Wire for right goal, Wire1 for left goal), buttons, power switch, speaker.
3. **INTERNAL STATE** — sensor objects, the `GameState` enum, score/clock variables, debounce and display-refresh tracking.
4. **VL53L0X READING** — `readVL53L0X()` has a subtle recovery path: `RangeStatus == 4` with a recent close reading is treated as "ball blocking the sensor" rather than out-of-range. Preserve this behaviour.
5. **SOUND / DISPLAY HELPERS / SCREEN DRAWING** — PWM tones via `ledc`, partial-redraw helpers that compare against `lastDisplayed*` to avoid flicker. Call `forceFullRedraw = true` whenever the screen layout needs to be rebuilt.
6. **GOAL LOGIC** — `checkGoals()` runs a rolling average over `SENSOR_AVG_SAMPLES`, debounces via `GOAL_DETECT_DURATION_MS`, and enforces `GOAL_COOLDOWN_MS` per sensor. `processGoal(leftSensor)` applies **side-swap** logic: in the first half the left sensor scores for *away* (right-hand team's goal is on the left from that team's perspective); in the second half teams swap sides so the mapping inverts. Any change to scoring must preserve this.
7. **CLOCK / BUTTON LOGIC** — clock advances `STATE_FIRST_HALF → HALFTIME → SECOND_HALF → FULLTIME` automatically on elapsed millis; BTN1 advances on user input, BTN2 confirms goals.
8. **SETUP / LOOP** — `loop()` gates everything on the power switch (GPIO 33, LOW = ON); switching off blanks the display, switching on resets the game to `PREGAME`. The goal flash animation and `STATE_GOAL_CONFIRM` short-circuit the main loop via early `return`s — be careful when adding work to `loop()`.

## Things to watch

- `TFT_CS_PIN` is `-1` (CS tied low in hardware). Don't "fix" it to a real pin.
- The two VL53L0X sensors share I2C address `0x29` and are kept separate by running them on `Wire` and `Wire1` respectively. Don't collapse them onto one bus without adding XSHUT-based address reassignment.
- GPIO 2 is deliberately avoided for buttons (boot-strap pin / onboard LED).
- When the clock pauses for `STATE_GOAL_CONFIRM`, `halfStartMillis` is rewritten on resume as `millis() - elapsedBeforeGoal` so the countdown continues from where it paused — don't reset `halfStartMillis` to `millis()` there.
