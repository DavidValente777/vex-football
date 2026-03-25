/*
 * VEX Football Arena Scoreboard
 * ESP32 + 2.8" TFT (ILI9341) + 2x HC-SR04 + Push Button
 *
 * Libraries required:
 *   - Adafruit_GFX
 *   - Adafruit_ILI9341
 */

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// ============================================================
// =================== CONFIGURABLE SETTINGS ==================
// ============================================================

// Team colours (16-bit RGB565 format)
#define HOME_COLOUR_R  0
#define HOME_COLOUR_G  255
#define HOME_COLOUR_B  0

#define AWAY_COLOUR_R  0
#define AWAY_COLOUR_G  120
#define AWAY_COLOUR_B  255

// Match duration per half in minutes
#define HALF_DURATION_MINUTES  5

// Ultrasonic goal detection: the ball must be detected (distance < threshold)
// for at least this many milliseconds continuously to count as a goal.
#define GOAL_DETECT_DURATION_MS  500

// Ultrasonic threshold in mm. If the reading is below this, a ball is present.
// Empty goal reads ~105mm (L) and ~99-155mm (R), so 70mm is safely below noise.
#define GOAL_THRESHOLD_MM  70

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

// Home ultrasonic sensor (left side of scoreboard)
#define HOME_TRIG_PIN  16
#define HOME_ECHO_PIN  17

// Away ultrasonic sensor (right side of scoreboard)
#define AWAY_TRIG_PIN  25
#define AWAY_ECHO_PIN  33

// Push button (active LOW with internal pull-up)
// Avoid GPIO 2 — it's a boot strapping pin with onboard LED on most ESP32 boards
#define BUTTON_PIN  4

// Confirm/resume button after a goal (active LOW with internal pull-up)
#define CONFIRM_BUTTON_PIN  15

// ============================================================
// ====================== INTERNAL STATE ======================
// ============================================================

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);

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
// ==================== ULTRASONIC READING ====================
// ============================================================

long readDistanceMm(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000);
  if (duration == 0) {
    return 9999;
  }
  long distanceMm = (duration * 343) / 2000;
  return distanceMm;
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
  tft.print("Home");

  tft.setTextColor(awayColour, ILI9341_BLACK);
  int awayX = tft.width() - 10 - getTextWidth("Away", 2);
  tft.setCursor(awayX, 40);
  tft.print("Away");

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
      uint16_t colour = (goalScorerTeam == "Home") ? homeColour : awayColour;
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
    uint16_t colour = (goalScorerTeam == "Home") ? homeColour : awayColour;
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
      goalScorerTeam = "Away";
    } else {
      homeScore++;
      goalScorerTeam = "Home";
    }
  } else {
    if (isFirstHalf) {
      homeScore++;
      goalScorerTeam = "Home";
    } else {
      awayScore++;
      goalScorerTeam = "Away";
    }
  }

  // Save current half so we can resume after confirmation
  stateBeforeGoal = gameState;
  elapsedBeforeGoal = millis() - halfStartMillis;

  goalFlashing = true;
  goalFlashStart = millis();
}

void checkGoals() {
  if (gameState != STATE_FIRST_HALF && gameState != STATE_SECOND_HALF) {
    return;
  }

  unsigned long now = millis();

  // --- Read both sensors ---
  long leftDist = readDistanceMm(HOME_TRIG_PIN, HOME_ECHO_PIN);
  long rightDist = readDistanceMm(AWAY_TRIG_PIN, AWAY_ECHO_PIN);

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

  // Ultrasonic pins
  pinMode(HOME_TRIG_PIN, OUTPUT);
  pinMode(HOME_ECHO_PIN, INPUT);
  pinMode(AWAY_TRIG_PIN, OUTPUT);
  pinMode(AWAY_ECHO_PIN, INPUT);

  // Buttons with internal pull-up
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(CONFIRM_BUTTON_PIN, INPUT_PULLUP);

  // Compute 16-bit colours
  homeColour = tft.color565(HOME_COLOUR_R, HOME_COLOUR_G, HOME_COLOUR_B);
  awayColour = tft.color565(AWAY_COLOUR_R, AWAY_COLOUR_G, AWAY_COLOUR_B);

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
      forceFullRedraw = true;
    }
  }

  // Wait for confirm button press after a goal
  if (gameState == STATE_GOAL_CONFIRM) {
    if (digitalRead(CONFIRM_BUTTON_PIN) == LOW) {
      unsigned long now = millis();
      if (now - lastConfirmPress >= BUTTON_DEBOUNCE_MS) {
        lastConfirmPress = now;
        while (digitalRead(CONFIRM_BUTTON_PIN) == LOW) {
          delay(10);
        }
        // Resume the half, adjusting the clock so it continues from where it paused
        gameState = stateBeforeGoal;
        halfStartMillis = millis() - elapsedBeforeGoal;
        forceFullRedraw = true;
      }
    }
    // Don't check goals or clock while waiting for confirmation
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

  delay(50);
}
