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

1. **CONFIGURABLE SETTINGS** — team names/colours, half duration, per-sensor goal thresholds (`LEFT_GOAL_THRESHOLD_MM` / `RIGHT_GOAL_THRESHOLD_MM` — tuned independently because the two sensors report slightly different idle distances), detection timings, and `SENSOR_AVG_SAMPLES`. Tune these before touching logic.
2. **PIN DEFINITIONS** — TFT, two I2C buses (Wire for right goal, Wire1 for left goal), three buttons (BTN1 start/advance on GPIO 4, BTN2 resume/confirm on GPIO 15, BTN3 stop/pause on GPIO 5), power switch (GPIO 33), speaker (GPIO 13).
3. **INTERNAL STATE** — sensor objects, the `GameState` enum (includes `STATE_PAUSED` and `STATE_GOAL_CONFIRM`), score/clock variables, separate `stateBeforeGoal`/`stateBeforePause` + `elapsedBeforeGoal`/`elapsedBeforePause` for the two kinds of clock pause, debounce and display-refresh tracking.
4. **I2C SCANNER** — `scanI2CBus()` runs at boot against both `Wire` and `Wire1` so serial logs show which sensor responded on which bus. Handy for wiring debugging.
5. **VL53L0X READING** — `readVL53L0X()` has a subtle recovery path: `RangeStatus == 4` with a recent close reading is treated as "ball blocking the sensor" rather than out-of-range. Preserve this behaviour.
6. **SOUND / DISPLAY HELPERS / SCREEN DRAWING** — PWM tones via `ledc`. Three sound cues: `playButtonBeep()` (short 1 kHz blip on any button press), `playGoalCelebration()` (ascending C-major melody on a goal), and `playFulltimeFanfare()` (referee-whistle + victory melody when the second half expires). Partial-redraw helpers compare against `lastDisplayed*` to avoid flicker — call `forceFullRedraw = true` whenever the screen layout needs to be rebuilt.
7. **GOAL LOGIC** — `checkGoals()` runs a rolling average over `SENSOR_AVG_SAMPLES`, debounces via `GOAL_DETECT_DURATION_MS`, and enforces `GOAL_COOLDOWN_MS` per sensor. `processGoal(leftSensor)` applies **side-swap** logic: in the first half the left sensor scores for *away* (right-hand team's goal is on the left from that team's perspective); in the second half teams swap sides so the mapping inverts. Any change to scoring must preserve this.
8. **CLOCK / BUTTON LOGIC** — clock advances `STATE_FIRST_HALF → HALFTIME → SECOND_HALF → FULLTIME` automatically on elapsed millis (fulltime triggers the fanfare). `checkButton()` handles BTN1 (advance state). BTN2 and BTN3 are polled directly in `loop()`.
9. **SETUP / LOOP** — `loop()` gates everything on the power switch (GPIO 33, LOW = ON); switching off blanks the display, switching on resets the game to `PREGAME`. BTN3 (stop) has two behaviours measured by hold length: a short press during a running half enters `STATE_PAUSED` (BTN2 resumes), a long press (≥ 5000 ms) in any state resets the game to `PREGAME`. The goal flash animation, `STATE_GOAL_CONFIRM`, and `STATE_PAUSED` each short-circuit the main loop via early `return`s — be careful when adding work to `loop()`.

## Things to watch

- `TFT_CS_PIN` is `-1` (CS tied low in hardware). Don't "fix" it to a real pin.
- The two VL53L0X sensors share I2C address `0x29` and are kept separate by running them on `Wire` and `Wire1` respectively. Don't collapse them onto one bus without adding XSHUT-based address reassignment.
- GPIO 2 is deliberately avoided for buttons (boot-strap pin / onboard LED).
- Two independent pause paths exist: `STATE_GOAL_CONFIRM` (stores `stateBeforeGoal` / `elapsedBeforeGoal`) and `STATE_PAUSED` (stores `stateBeforePause` / `elapsedBeforePause`). Both restore the clock on resume with `halfStartMillis = millis() - elapsedBefore*`. Don't reset `halfStartMillis` to `millis()` in either resume path, and don't reuse one pair of variables for both — they can't nest but keeping them separate documents intent.
- The stop-button long-press reset duplicates the power-cycle reset logic. If you change what "reset" means, update both call sites.
