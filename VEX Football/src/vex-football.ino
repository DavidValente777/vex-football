/*
 * VEX Football Arena Scoreboard
 * ESP32 + 2.8" TFT (ILI9341) + 2x HC-SR04 + Push Button
 *
 * Libraries required:
 *   - Adafruit_GFX
 *   - Adafruit_ILI9341
 *   - Adafruit_VL53L0X (both goal sensors via I2C)
 */

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Adafruit_VL53L0X.h>

// ============================================================
// =================== CONFIGURABLE SETTINGS ===============/btw ===
// ============================================================

// Team names
#define HOME_NAME  "David"
#define AWAY_NAME  "Ronnie"

// Predefined team colours (RGB565 format)
// Pick from: RED, ORANGE, YELLOW, GREEN, CYAN, BLUE, PURPLE, PINK, WHITE
#define COLOUR_RED     0xF800
#define COLOUR_ORANGE  0xFD20
#define COLOUR_YELLOW  0xFFE0
#define COLOUR_GREEN   0x07E0
#define COLOUR_CYAN    0x07FF
#define COLOUR_BLUE    0x001F
#define COLOUR_PURPLE  0x780F
#define COLOUR_PINK    0xF81F
#define COLOUR_WHITE   0xFFFF

// Set each team's colour here
#define HOME_COLOUR  COLOUR_GREEN
#define AWAY_COLOUR  COLOUR_BLUE

// Match duration per half in minutes
#define HALF_DURATION_MINUTES  5

// Goal detection: the ball must be detected (distance < threshold)
// for at least this many milliseconds continuously to count as a goal.
#define GOAL_DETECT_DURATION_MS  150

// Goal threshold in mm (both sensors)
// Empty goals read ~175-180mm. Set below idle but above ball distance.
#define GOAL_THRESHOLD_MM  160

// Minimum valid distance in mm. Readings below this are treated as noise.
#define GOAL_MIN_VALID_MM  10

// Cooldown after a goal before the same sensor can score again (ms)
#define GOAL_COOLDOWN_MS  3000

// ============================================================
// ====================== PIN DEFINITIONS =====================
// ============================================================

// TFT display pins (matching your working legacy wiring)
#define TFT_CS_PIN   -1
#define TFT_DC_PIN   27
#define TFT_RST_PIN  26

// VL53L0X time-of-flight sensors on separate I2C buses
// Left goal:  Wire1 — SDA=32, SCL=25
// Right goal: Wire  — SDA=21, SCL=22 (ESP32 default)
#define LEFT_I2C_SDA   32
#define LEFT_I2C_SCL   25

// Push button (active LOW with internal pull-up)
// Avoid GPIO 2 — it's a boot strapping pin with onboard LED on most ESP32 boards
#define BUTTON_PIN  4

// Confirm/resume button after a goal (active LOW with internal pull-up)
#define CONFIRM_BUTTON_PIN  15

// Speaker pin
#define SPEAKER_PIN  13
#define SPEAKER_PWM_CHANNEL  0

// ============================================================
// ====================== INTERNAL STATE ======================
// ============================================================

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);
Adafruit_VL53L0X leftSensor = Adafruit_VL53L0X();
Adafruit_VL53L0X rightSensor = Adafruit_VL53L0X();
bool leftSensorReady = false;
bool rightSensorReady = false;

uint16_t homeColour;
uint16_t awayColour;

// Game state machine
enum GameState {
  STATE_PREGAME,      // Waiting for button press to start first half
  STATE_FIRST_HALF,   // First half running
  STATE_HALFTIME,     // Halftime — waiting for button press
  STATE_SECOND_HALF,  // Second half running
  STATE_FULLTIME,     // Game over — waiting for button press to reset
  STATE_GOAL_CONFIRM  // Goal scored — waiting for confirm button to resume
};

GameState gameState = STATE_PREGAME;

// Scores
int homeScore = 0;
int awayScore = 0;

// Clock
unsigned long halfStartMillis = 0;
unsigned long halfDurationMs = (unsigned long)HALF_DURATION_MINUTES * 60UL * 1000UL;
unsigned long elapsedMs = 0;

// Goal detection state for each sensor
unsigned long leftSensorBelowSince = 0;
bool leftSensorTriggered = false;
bool leftGoalCounted = false;
unsigned long leftGoalTime = 0;

unsigned long rightSensorBelowSince = 0;
bool rightSensorTriggered = false;
bool rightGoalCounted = false;
unsigned long rightGoalTime = 0;

// Sensor debug print throttle
unsigned long lastSensorPrint = 0;
#define SENSOR_PRINT_INTERVAL_MS  500

// Goal flash animation
unsigned long goalFlashStart = 0;
bool goalFlashing = false;
String goalScorerTeam = "";
#define GOAL_FLASH_DURATION_MS  2000
#define GOAL_FLASH_INTERVAL_MS  250

// Button debounce
unsigned long lastButtonPress = 0;
unsigned long lastConfirmPress = 0;
#define BUTTON_DEBOUNCE_MS  300

// Track which half to resume after goal confirmation
GameState stateBeforeGoal = STATE_FIRST_HALF;
// Track elapsed time so the clock pauses during goal confirmation
unsigned long elapsedBeforeGoal = 0;

// Display refresh tracking
int lastDisplayedHomeScore = -1;
int lastDisplayedAwayScore = -1;
int lastDisplayedMinute = -1;
int lastDisplayedSecond = -1;
GameState lastDisplayedState = STATE_PREGAME;
bool forceFullRedraw = true;

// ============================================================
// =================== VL53L0X READING ========================
// ============================================================

long readVL53L0X(Adafruit_VL53L0X &sensor) {
  VL53L0X_RangingMeasurementData_t measure;
  sensor.rangingTest(&measure, false);
  if (measure.RangeStatus != 4) {
    return (long)measure.RangeMilliMeter;
  }
  return 9999; // Out of range
}

// ============================================================
// ======================== SOUND =============================
// ============================================================

void playTone(int frequency, int durationMs) {
  ledcWriteTone(SPEAKER_PWM_CHANNEL, frequency);
  delay(durationMs);
  ledcWriteTone(SPEAKER_PWM_CHANNEL, 0);
}

void playButtonBeep() {
  playTone(1000, 50);
}

void playGoalCelebration() {
  // Ascending celebration melody
  playTone(523, 100);  // C5
  delay(30);
  playTone(659, 100);  // E5
  delay(30);
  playTone(784, 100);  // G5
  delay(30);
  playTone(1047, 200); // C6
  delay(50);
  playTone(784, 100);  // G5
  delay(30);
  playTone(1047, 300); // C6 (long finish)
}

// ============================================================
// ===================== DISPLAY HELPERS ======================
// ============================================================

// Helper to measure text width (Adafruit_GFX doesn't have textWidth)
int16_t getTextWidth(const char* text, int fontSize) {
  int16_t x1, y1;
  uint16_t w, h;
  tft.setTextSize(fontSize);
  tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  return (int16_t)w;
}

void drawCenteredText(const char* text, int y, uint16_t colour, int fontSize) {
  tft.setTextColor(colour, ILI9341_BLACK);
  tft.setTextSize(fontSize);
  int16_t textW = getTextWidth(text, fontSize);
  int16_t x = (tft.width() - textW) / 2;
  tft.setCursor(x, y);
  tft.print(text);
}

void clearLine(int y, int height) {
  tft.fillRect(0, y, tft.width(), height, ILI9341_BLACK);
}

// ============================================================
// ==================== SCREEN DRAWING ========================
// ============================================================

void drawFullScoreboard() {
  tft.fillScreen(ILI9341_BLACK);

  // Title bar
  drawCenteredText("VEX FOOTBALL", 5, ILI9341_WHITE, 2);

  // Draw team names
  tft.setTextSize(2);
  tft.setTextColor(homeColour, ILI9341_BLACK);
  tft.setCursor(10, 40);
  tft.print(HOME_NAME);

  tft.setTextColor(awayColour, ILI9341_BLACK);
  int awayX = tft.width() - 10 - getTextWidth(AWAY_NAME, 2);
  tft.setCursor(awayX, 40);
  tft.print(AWAY_NAME);

  // Draw colon between scores
  drawCenteredText(":", 75, ILI9341_WHITE, 4);

  // Scores
  drawScores();

  // Clock
  drawClock();

  // State indicator
  drawStateIndicator();

  forceFullRedraw = false;
  lastDisplayedHomeScore = homeScore;
  lastDisplayedAwayScore = awayScore;
}

void drawScores() {
  char buf[4];

  // Home score (left side)
  tft.setTextSize(4);
  sprintf(buf, "%d", homeScore);
  int scoreWidth = getTextWidth(buf, 4);
  int centreX = tft.width() / 2;
  int homeScoreX = centreX - 20 - scoreWidth;
  tft.fillRect(10, 70, centreX - 20, 35, ILI9341_BLACK);
  tft.setTextColor(homeColour, ILI9341_BLACK);
  tft.setCursor(homeScoreX, 75);
  tft.print(buf);

  // Away score (right side)
  sprintf(buf, "%d", awayScore);
  int awayScoreX = centreX + 20;
  tft.fillRect(centreX + 15, 70, centreX - 15, 35, ILI9341_BLACK);
  tft.setTextColor(awayColour, ILI9341_BLACK);
  tft.setCursor(awayScoreX, 75);
  tft.print(buf);

  lastDisplayedHomeScore = homeScore;
  lastDisplayedAwayScore = awayScore;
}

void drawClock() {
  unsigned long remaining = 0;
  if (gameState == STATE_FIRST_HALF || gameState == STATE_SECOND_HALF) {
    unsigned long elapsed = millis() - halfStartMillis;
    if (elapsed >= halfDurationMs) {
      remaining = 0;
    } else {
      remaining = halfDurationMs - elapsed;
    }
  } else if (gameState == STATE_PREGAME || gameState == STATE_HALFTIME) {
    remaining = halfDurationMs;
  } else if (gameState == STATE_GOAL_CONFIRM) {
    // Show the paused clock time
    if (elapsedBeforeGoal >= halfDurationMs) {
      remaining = 0;
    } else {
      remaining = halfDurationMs - elapsedBeforeGoal;
    }
  }

  int totalSeconds = remaining / 1000;
  int minutes = totalSeconds / 60;
  int seconds = totalSeconds % 60;

  if (minutes != lastDisplayedMinute || seconds != lastDisplayedSecond || forceFullRedraw) {
    char timeBuf[8];
    sprintf(timeBuf, "%02d:%02d", minutes, seconds);
    clearLine(125, 25);
    drawCenteredText(timeBuf, 125, ILI9341_YELLOW, 3);
    lastDisplayedMinute = minutes;
    lastDisplayedSecond = seconds;
  }
}

void drawStateIndicator() {
  clearLine(165, 70);
  switch (gameState) {
    case STATE_PREGAME:
      drawCenteredText("Press button", 170, ILI9341_WHITE, 2);
      drawCenteredText("to start!", 195, ILI9341_WHITE, 2);
      break;
    case STATE_FIRST_HALF:
      drawCenteredText("1st Half", 180, ILI9341_GREEN, 2);
      break;
    case STATE_HALFTIME:
      drawCenteredText("HALF TIME", 165, ILI9341_ORANGE, 2);
      drawCenteredText("Switch sides!", 190, ILI9341_RED, 2);
      drawCenteredText("Press to resume", 215, ILI9341_WHITE, 1);
      break;
    case STATE_SECOND_HALF:
      drawCenteredText("2nd Half", 180, ILI9341_GREEN, 2);
      break;
    case STATE_GOAL_CONFIRM: {
      uint16_t colour = (goalScorerTeam == HOME_NAME) ? homeColour : awayColour;
      char buf[24];
      sprintf(buf, "%s scored!", goalScorerTeam.c_str());
      drawCenteredText(buf, 165, colour, 2);
      drawCenteredText("Press BTN2", 190, ILI9341_WHITE, 2);
      drawCenteredText("to resume", 215, ILI9341_WHITE, 2);
      break;
    }
    case STATE_FULLTIME:
      drawCenteredText("FULL TIME!", 170, ILI9341_RED, 2);
      if (homeScore > awayScore) {
        drawCenteredText("Home wins!", 195, homeColour, 2);
      } else if (awayScore > homeScore) {
        drawCenteredText("Away wins!", 195, awayColour, 2);
      } else {
        drawCenteredText("It's a draw!", 195, ILI9341_WHITE, 2);
      }
      break;
  }
  lastDisplayedState = gameState;
}

void drawGoalFlash() {
  unsigned long elapsed = millis() - goalFlashStart;
  bool showGoal = ((elapsed / GOAL_FLASH_INTERVAL_MS) % 2) == 0;

  if (showGoal) {
    uint16_t colour = (goalScorerTeam == HOME_NAME) ? homeColour : awayColour;
    tft.fillScreen(ILI9341_BLACK);

    drawCenteredText("GOAL!!!", 50, colour, 4);

    char buf[24];
    sprintf(buf, "%s scores!", goalScorerTeam.c_str());
    drawCenteredText(buf, 110, colour, 2);

    char scoreBuf[16];
    sprintf(scoreBuf, "%d : %d", homeScore, awayScore);
    drawCenteredText(scoreBuf, 160, ILI9341_WHITE, 3);
  } else {
    tft.fillScreen(ILI9341_BLACK);
  }
}

// ============================================================
// ====================== GOAL LOGIC ==========================
// ============================================================

void processGoal(bool leftSensor) {
  bool isFirstHalf = (gameState == STATE_FIRST_HALF);

  if (leftSensor) {
    if (isFirstHalf) {
      awayScore++;
      goalScorerTeam = AWAY_NAME;
    } else {
      homeScore++;
      goalScorerTeam = HOME_NAME;
    }
  } else {
    if (isFirstHalf) {
      homeScore++;
      goalScorerTeam = HOME_NAME;
    } else {
      awayScore++;
      goalScorerTeam = AWAY_NAME;
    }
  }

  // Save current half so we can resume after confirmation
  stateBeforeGoal = gameState;
  elapsedBeforeGoal = millis() - halfStartMillis;

  playGoalCelebration();

  goalFlashing = true;
  goalFlashStart = millis();
}

void checkGoals() {
  if (gameState != STATE_FIRST_HALF && gameState != STATE_SECOND_HALF) {
    return;
  }

  unsigned long now = millis();

  // --- Read both sensors ---
  long leftDist = leftSensorReady ? readVL53L0X(leftSensor) : 9999;
  long rightDist = rightSensorReady ? readVL53L0X(rightSensor) : 9999;

  // --- Debug: print distances periodically ---
  if (now - lastSensorPrint >= SENSOR_PRINT_INTERVAL_MS) {
    Serial.printf("L: %ld mm  R: %ld mm\n", leftDist, rightDist);
    lastSensorPrint = now;
  }

  // --- Left sensor ---
  bool leftValid = (leftDist > GOAL_MIN_VALID_MM && leftDist < GOAL_THRESHOLD_MM);
  bool leftCooldownOk = (now - leftGoalTime > GOAL_COOLDOWN_MS);

  if (leftValid) {
    if (!leftSensorTriggered) {
      leftSensorTriggered = true;
      leftSensorBelowSince = now;
    } else if (!leftGoalCounted && leftCooldownOk &&
               (now - leftSensorBelowSince >= GOAL_DETECT_DURATION_MS)) {
      leftGoalCounted = true;
      leftGoalTime = now;
      Serial.printf("GOAL! Left sensor at %ld mm\n", leftDist);
      processGoal(true);
    }
  } else {
    leftSensorTriggered = false;
    leftGoalCounted = false;
  }

  // --- Right sensor ---
  bool rightValid = (rightDist > GOAL_MIN_VALID_MM && rightDist < GOAL_THRESHOLD_MM);
  bool rightCooldownOk = (now - rightGoalTime > GOAL_COOLDOWN_MS);

  if (rightValid) {
    if (!rightSensorTriggered) {
      rightSensorTriggered = true;
      rightSensorBelowSince = now;
    } else if (!rightGoalCounted && rightCooldownOk &&
               (now - rightSensorBelowSince >= GOAL_DETECT_DURATION_MS)) {
      rightGoalCounted = true;
      rightGoalTime = now;
      Serial.printf("GOAL! Right sensor at %ld mm\n", rightDist);
      processGoal(false);
    }
  } else {
    rightSensorTriggered = false;
    rightGoalCounted = false;
  }
}

// ============================================================
// ===================== CLOCK LOGIC ==========================
// ============================================================

void checkClock() {
  if (gameState != STATE_FIRST_HALF && gameState != STATE_SECOND_HALF) {
    return;
  }

  unsigned long elapsed = millis() - halfStartMillis;
  if (elapsed >= halfDurationMs) {
    if (gameState == STATE_FIRST_HALF) {
      gameState = STATE_HALFTIME;
    } else {
      gameState = STATE_FULLTIME;
    }
    forceFullRedraw = true;
  }
}

// ============================================================
// ==================== BUTTON LOGIC ==========================
// ============================================================

void checkButton() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    unsigned long now = millis();
    if (now - lastButtonPress < BUTTON_DEBOUNCE_MS) {
      return;
    }
    lastButtonPress = now;
    Serial.printf("Button pressed! State: %d\n", gameState);
    playButtonBeep();

    while (digitalRead(BUTTON_PIN) == LOW) {
      delay(10);
    }

    switch (gameState) {
      case STATE_PREGAME:
        gameState = STATE_FIRST_HALF;
        halfStartMillis = millis();
        forceFullRedraw = true;
        break;

      case STATE_HALFTIME:
        gameState = STATE_SECOND_HALF;
        halfStartMillis = millis();
        forceFullRedraw = true;
        break;

      case STATE_FULLTIME:
        homeScore = 0;
        awayScore = 0;
        gameState = STATE_PREGAME;
        goalFlashing = false;
        leftSensorTriggered = false;
        leftGoalCounted = false;
        rightSensorTriggered = false;
        rightGoalCounted = false;
        forceFullRedraw = true;
        break;

      default:
        break;
    }
  }
}

// ============================================================
// ======================== SETUP =============================
// ============================================================

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=== VEX Football Scoreboard ===");

  // Init I2C buses for goal sensors
  Wire.begin(21, 22);                        // Right goal
  Wire1.begin(LEFT_I2C_SDA, LEFT_I2C_SCL);  // Left goal
  delay(50);

  // Init right sensor (high speed short range mode)
  if (rightSensor.begin(0x29, false, &Wire, Adafruit_VL53L0X::VL53L0X_SENSE_HIGH_SPEED)) {
    rightSensorReady = true;
    Serial.println("VL53L0X (right goal) OK - high speed mode");
  } else {
    Serial.println("ERROR: VL53L0X (right goal) not found!");
  }

  // Init left sensor (high speed short range mode)
  if (leftSensor.begin(0x29, false, &Wire1, Adafruit_VL53L0X::VL53L0X_SENSE_HIGH_SPEED)) {
    leftSensorReady = true;
    Serial.println("VL53L0X (left goal) OK - high speed mode");
  } else {
    Serial.println("ERROR: VL53L0X (left goal) not found!");
  }

  // Buttons with internal pull-up
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(CONFIRM_BUTTON_PIN, INPUT_PULLUP);

  // Speaker
  ledcSetup(SPEAKER_PWM_CHANNEL, 2000, 8);
  ledcAttachPin(SPEAKER_PIN, SPEAKER_PWM_CHANNEL);

  // Set team colours
  homeColour = HOME_COLOUR;
  awayColour = AWAY_COLOUR;

  // Init display
  Serial.println("Initializing TFT...");
  tft.begin();
  tft.setRotation(1); // Landscape
  tft.fillScreen(ILI9341_BLACK);
  Serial.printf("Display size: %d x %d\n", tft.width(), tft.height());

  drawFullScoreboard();
  Serial.printf("Button pin %d reads: %d\n", BUTTON_PIN, digitalRead(BUTTON_PIN));
  Serial.printf("Confirm pin %d reads: %d\n", CONFIRM_BUTTON_PIN, digitalRead(CONFIRM_BUTTON_PIN));
  Serial.println("Setup complete!");
}

// ============================================================
// ========================= LOOP =============================
// ============================================================

void loop() {
  checkButton();

  // Handle goal flash animation
  if (goalFlashing) {
    if (millis() - goalFlashStart < GOAL_FLASH_DURATION_MS) {
      drawGoalFlash();
      delay(50);
      return;
    } else {
      goalFlashing = false;
      gameState = STATE_GOAL_CONFIRM;
      // Draw the scoreboard immediately with the goal confirm message
      drawFullScoreboard();
      Serial.println("Goal confirmed — waiting for BTN2");
    }
  }

  // Wait for confirm button press after a goal
  if (gameState == STATE_GOAL_CONFIRM) {
    if (digitalRead(CONFIRM_BUTTON_PIN) == LOW) {
      unsigned long now = millis();
      if (now - lastConfirmPress >= BUTTON_DEBOUNCE_MS) {
        lastConfirmPress = now;
        Serial.println("Confirm button pressed — resuming");
        playButtonBeep();
        while (digitalRead(CONFIRM_BUTTON_PIN) == LOW) {
          delay(10);
        }
        // Resume the half, adjusting the clock so it continues from where it paused
        gameState = stateBeforeGoal;
        halfStartMillis = millis() - elapsedBeforeGoal;
        forceFullRedraw = true;
      }
    }
    delay(50);
    return;
  }

  checkGoals();
  checkClock();

  // Update display
  if (forceFullRedraw || lastDisplayedState != gameState) {
    drawFullScoreboard();
  } else {
    if (homeScore != lastDisplayedHomeScore || awayScore != lastDisplayedAwayScore) {
      drawScores();
    }
    drawClock();
  }

  delay(20);
}
