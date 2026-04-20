# VEX Football Arena Scoreboard

An ESP32-based scoreboard system for a VEX robot football arena. Two VL53L0X time-of-flight sensors detect goals, a 2.8" TFT display shows live scores and a countdown clock, and a speaker plays celebration sounds.

## How It Works

Two VEX robots play football on a small arena. Each goal has a VL53L0X laser distance sensor mounted inside the net. When a ball enters the goal, the sensor reads a shorter distance than the empty-goal baseline (~175-180mm). If the averaged reading stays below the per-goal threshold (145mm left / 143mm right by default) for at least 150ms, a goal is counted.

A game consists of two halves (default 3 minutes each). Teams swap sides at halftime, and the scoring logic accounts for this automatically. After each goal, the clock pauses and a confirmation button must be pressed to resume play. A stop button can pause a running half at any time, or long-press it for 5 seconds to reset the game. When the second half expires, the board plays a victory fanfare.

## Game State Machine

```
PREGAME ──[BTN1]──> FIRST_HALF ──[clock expires]──> HALFTIME
                        |                               |
                   [goal detected]                   [BTN1]
                        |                               |
                   GOAL_CONFIRM                    SECOND_HALF ──[clock expires]──> FULLTIME (fanfare)
                        |                               |                              |
                   [BTN2 resume]                   [goal detected]                  [BTN1]
                        |                               |                              |
                   (back to half)               GOAL_CONFIRM ──[BTN2]──> (back)    PREGAME

Any running half ──[BTN3 short press]──> PAUSED ──[BTN2 resume]──> (back to half)
Any state        ──[BTN3 held ≥ 5 s]──> PREGAME (full reset)
```

- **BTN1** (GPIO 4): Advances game state (start, resume after halftime, reset after fulltime)
- **BTN2** (GPIO 15): Confirms a goal and resumes play; also resumes from pause
- **BTN3** (GPIO 5): Short press pauses a running half; hold ≥ 5 s to reset the game to pregame

## Hardware

| Component | Description |
|-----------|-------------|
| ESP32 Dev Module | Main microcontroller |
| 2.8" ILI9341 TFT LCD | Scoreboard display (320x240, SPI) |
| 2x VL53L0X | Time-of-flight laser distance sensors (one per goal) |
| 3x Momentary push buttons | Start/advance, goal confirm/resume, stop/pause (active LOW, internal pull-up) |
| 1x Toggle switch | Power on/off (wired to GND, internal pull-up) |
| Speaker/buzzer | Button feedback, goal celebration, and fulltime fanfare (PWM) |

## Wiring

### TFT Display (SPI)

| TFT Pin | ESP32 Pin |
|---------|-----------|
| GND | GND |
| VCC | 3.3V |
| CLK | GPIO 18 |
| MOSI | GPIO 23 |
| MISO | GPIO 19 |
| RES | GPIO 26 |
| DC | GPIO 27 |
| BLK | 3.3V |
| CS | Not connected (CS = -1) |

### Left Goal Sensor — VL53L0X (I2C bus: Wire1)

| Sensor Pin | ESP32 Pin |
|------------|-----------|
| VCC | 3.3V |
| GND | GND |
| SDA | GPIO 32 |
| SCL | GPIO 25 |

### Right Goal Sensor — VL53L0X (I2C bus: Wire)

| Sensor Pin | ESP32 Pin |
|------------|-----------|
| VCC | 3.3V |
| GND | GND |
| SDA | GPIO 21 |
| SCL | GPIO 22 |

### Buttons

| Button | ESP32 Pin | Notes |
|--------|-----------|-------|
| Start / advance (BTN1) | GPIO 4 | Active LOW, internal pull-up |
| Confirm / resume (BTN2) | GPIO 15 | Active LOW, internal pull-up. Confirms a goal and also resumes from pause. |
| Stop / pause (BTN3) | GPIO 5 | Active LOW, internal pull-up. Short press pauses a running half; hold ≥ 5 s to reset the game. |

### Power Toggle Switch

| Switch Pin | ESP32 Pin |
|------------|-----------|
| One leg | GPIO 33 |
| Other leg | GND |

When the switch is ON (closed), GPIO 33 is pulled to GND (LOW) and the game runs. When OFF (open), the internal pull-up pulls the pin HIGH, the display goes blank, and all game logic stops. Flipping it back ON resets the game to the pregame state.

### Speaker

| Speaker | ESP32 Pin |
|---------|-----------|
| Signal | GPIO 13 (PWM channel 0) |

## Configurable Parameters

These are defined at the top of `VEX Football/src/vex-football.ino`:

| Parameter | Default | Description |
|-----------|---------|-------------|
| `HOME_NAME` / `AWAY_NAME` | "David" / "Ronnie" | Team names shown on display |
| `HOME_COLOUR` / `AWAY_COLOUR` | Green / Blue | Team colours (RGB565 presets available) |
| `HALF_DURATION_MINUTES` | 3 | Length of each half in minutes |
| `LEFT_GOAL_THRESHOLD_MM` | 145 | Distance below which the left sensor counts a ball |
| `RIGHT_GOAL_THRESHOLD_MM` | 143 | Distance below which the right sensor counts a ball |
| `GOAL_MIN_VALID_MM` | 10 | Readings below this are treated as noise |
| `GOAL_DETECT_DURATION_MS` | 150 | How long the ball must be present to count |
| `GOAL_COOLDOWN_MS` | 2000 | Cooldown before the same goal can score again |
| `SENSOR_AVG_SAMPLES` | 10 | Size of the rolling average used to smooth sensor noise |

## Building

This is a [PlatformIO](https://platformio.org/) project targeting `esp32dev`. To build and upload:

```bash
cd "VEX Football"
pio run -t upload
pio device monitor   # optional: view serial debug output
```

### Dependencies (managed by PlatformIO)

- Adafruit GFX Library
- Adafruit ILI9341
- Adafruit VL53L0X
