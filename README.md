# VEX Football Arena Scoreboard

An ESP32-based scoreboard system for a VEX robot football arena. Two VL53L0X time-of-flight sensors detect goals, a 2.8" TFT display shows live scores and a countdown clock, and a speaker plays celebration sounds.

## How It Works

Two VEX robots play football on a small arena. Each goal has a VL53L0X laser distance sensor mounted inside the net. When a ball enters the goal, the sensor reads a shorter distance than the empty-goal baseline (~175-180mm). If the reading stays below the threshold (160mm) for at least 150ms, a goal is counted.

A game consists of two halves (default 5 minutes each). Teams swap sides at halftime, and the scoring logic accounts for this automatically. After each goal, the clock pauses and a confirmation button must be pressed to resume play.

## Game State Machine

```
PREGAME ──[BTN1]──> FIRST_HALF ──[clock expires]──> HALFTIME
                        |                               |
                   [goal detected]                   [BTN1]
                        |                               |
                   GOAL_CONFIRM                    SECOND_HALF ──[clock expires]──> FULLTIME
                        |                               |                              |
                   [BTN2 resume]                   [goal detected]                  [BTN1]
                        |                               |                              |
                   (back to half)               GOAL_CONFIRM ──[BTN2]──> (back)    PREGAME
```

- **BTN1** (GPIO 4): Advances game state (start, resume after halftime, reset after fulltime)
- **BTN2** (GPIO 15): Confirms a goal and resumes play

## Hardware

| Component | Description |
|-----------|-------------|
| ESP32 Dev Module | Main microcontroller |
| 2.8" ILI9341 TFT LCD | Scoreboard display (320x240, SPI) |
| 2x VL53L0X | Time-of-flight laser distance sensors (one per goal) |
| 2x Momentary push buttons | Game control (active LOW, internal pull-up) |
| Speaker/buzzer | Goal celebration and button feedback (PWM) |

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
| Game control (BTN1) | GPIO 4 | Active LOW, internal pull-up |
| Goal confirm (BTN2) | GPIO 15 | Active LOW, internal pull-up |

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
| `HALF_DURATION_MINUTES` | 5 | Length of each half in minutes |
| `GOAL_THRESHOLD_MM` | 160 | Distance below which a ball is detected |
| `GOAL_DETECT_DURATION_MS` | 150 | How long the ball must be present to count |
| `GOAL_COOLDOWN_MS` | 3000 | Cooldown before the same goal can score again |

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
