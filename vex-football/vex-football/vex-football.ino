/*
 * VEX Football Arena Scoreboard
 * ESP32 + 2.8" TFT (ILI9341) + 2x HC-SR04 + Push Button
 *
 * Libraries required:
 *   - TFT_eSPI (configure User_Setup.h or use the provided setup below)
 *
 * TFT_eSPI User_Setup.h overrides (set these in the library's User_Setup.h):
 *   #define ILI9341_DRIVER
 *   #define TFT_MOSI  23
 *   #define TFT_MISO  19
 *   #define TFT_SCLK  18
 *   #define TFT_CS    -1   // Not connected (tie CS low on the display)
 *   #define TFT_DC    27
 *   #define TFT_RST   26
 *   #define SPI_FREQUENCY  40000000
 */

#include <TFT_eSPI.h>

// ============================================================
// =================== CONFIGURABLE SETTINGS ==================
// ============================================================

// Team colours (16-bit RGB565 format)
// Use tft.color565(r, g, b) at runtime, or define directly.
// Default: Home = Green, Away = Blue
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
#define GOAL_DETECT_DURATION_MS  300

// Ultrasonic threshold in mm. If the reading is below this, a ball is present.
// The empty goal reads ~150mm; a ball inside will read significantly less.
#define GOAL_THRESHOLD_MM  100

// ============================================================
// ====================== PIN DEFINITIONS =====================
// ============================================================

// Home ultrasonic sensor (left side of scoreboard)
#define HOME_TRIG_PIN  16
#define HOME_ECHO_PIN  17

// Away ultrasonic sensor (right side of scoreboard)
#define AWAY_TRIG_PIN  25
#define AWAY_ECHO_PIN  33

// Push button (active LOW with internal pull-up)
#define BUTTON_PIN  2

// ============================================================
// ====================== INTERNAL STATE ======================
// ============================================================

TFT_eSPI tft = TFT_eSPI();

uint16_t homeColour;
uint16_t awayColour;

// Game state machine
enum GameState {
  STATE_PREGAME,      // Waiting for button press to start first half
  STATE_FIRST_HALF,   // First half running
  STATE_HALFTIME,     // Halftime — waiting for button press
  STATE_SECOND_HALF,  // Second half running
  STATE_FULLTIME      // Game over — waiting for button press to reset
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
bool leftGoalCounted = false;  // Prevents counting the same ball twice

unsigned long rightSensorBelowSince = 0;
bool rightSensorTriggered = false;
bool rightGoalCounted = false;

// Goal flash animation
unsigned long goalFlashStart = 0;
bool goalFlashing = false;
String goalScorerTeam = "";
#define GOAL_FLASH_DURATION_MS  2000
#define GOAL_FLASH_INTERVAL_MS  250

// Button debounce
unsigned long lastButtonPress = 0;
#define BUTTON_DEBOUNCE_MS  300

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

  long duration = pulseIn(echoPin, HIGH, 30000); // timeout ~30ms (~5m max)
  if (duration == 0) {
    return 9999; // No echo — treat as far away
  }
  long distanceMm = (duration * 343) / 2000; // speed of sound ~343 m/s
  return distanceMm;
}

// ============================================================
// ===================== DISPLAY HELPERS ======================
// ============================================================

void drawCenteredText(const char* text, int y, uint16_t colour, int fontSize) {
  tft.setTextColor(colour, TFT_BLACK);
  tft.setTextSize(fontSize);
  int16_t textWidth = tft.textWidth(text);
  int16_t x = (tft.width() - textWidth) / 2;
  tft.setCursor(x, y);
  tft.print(text);
}

void clearLine(int y, int height) {
  tft.fillRect(0, y, tft.width(), height, TFT_BLACK);
}

// ============================================================
// ==================== SCREEN DRAWING ========================
// ============================================================

void drawFullScoreboard() {
  tft.fillScreen(TFT_BLACK);

  // Title bar
  drawCenteredText("VEX FOOTBALL", 5, TFT_WHITE, 2);

  // Draw team names
  tft.setTextSize(2);
  tft.setTextColor(homeColour, TFT_BLACK);
  tft.setCursor(10, 40);
  tft.print("Home");

  tft.setTextColor(awayColour, TFT_BLACK);
  int awayX = tft.width() - 10 - tft.textWidth("Away");
  tft.setCursor(awayX, 40);
  tft.print("Away");

  // Draw colon between scores
  drawCenteredText(":", 75, TFT_WHITE, 4);

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
  int scoreWidth = tft.textWidth(buf);
  // Position home score to the left of centre
  int centreX = tft.width() / 2;
  int homeScoreX = centreX - 20 - scoreWidth;
  tft.fillRect(10, 70, centreX - 20, 35, TFT_BLACK);
  tft.setTextColor(homeColour, TFT_BLACK);
  tft.setCursor(homeScoreX, 75);
  tft.print(buf);

  // Away score (right side)
  sprintf(buf, "%d", awayScore);
  int awayScoreX = centreX + 20;
  tft.fillRect(centreX + 15, 70, centreX - 15, 35, TFT_BLACK);
  tft.setTextColor(awayColour, TFT_BLACK);
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
  }
  // At fulltime, remaining = 0

  int totalSeconds = remaining / 1000;
  int minutes = totalSeconds / 60;
  int seconds = totalSeconds % 60;

  if (minutes != lastDisplayedMinute || seconds != lastDisplayedSecond || forceFullRedraw) {
    char timeBuf[8];
    sprintf(timeBuf, "%02d:%02d", minutes, seconds);
    clearLine(125, 25);
    drawCenteredText(timeBuf, 125, TFT_YELLOW, 3);
    lastDisplayedMinute = minutes;
    lastDisplayedSecond = seconds;
  }
}

void drawStateIndicator() {
  clearLine(165, 70);
  switch (gameState) {
    case STATE_PREGAME:
      drawCenteredText("Press button", 170, TFT_WHITE, 2);
      drawCenteredText("to start!", 195, TFT_WHITE, 2);
      break;
    case STATE_FIRST_HALF:
      drawCenteredText("1st Half", 180, TFT_GREEN, 2);
      break;
    case STATE_HALFTIME:
      drawCenteredText("HALF TIME", 165, TFT_ORANGE, 2);
      drawCenteredText("Switch sides!", 190, TFT_RED, 2);
      drawCenteredText("Press to resume", 215, TFT_WHITE, 1);
      break;
    case STATE_SECOND_HALF:
      drawCenteredText("2nd Half", 180, TFT_GREEN, 2);
      break;
    case STATE_FULLTIME:
      drawCenteredText("FULL TIME!", 170, TFT_RED, 2);
      if (homeScore > awayScore) {
        drawCenteredText("Home wins!", 195, homeColour, 2);
      } else if (awayScore > homeScore) {
        drawCenteredText("Away wins!", 195, awayColour, 2);
      } else {
        drawCenteredText("It's a draw!", 195, TFT_WHITE, 2);
      }
      break;
  }
  lastDisplayedState = gameState;
}

void drawGoalFlash() {
  unsigned long elapsed = millis() - goalFlashStart;
  // Alternate between showing "GOAL!" and showing the scoreboard
  bool showGoal = ((elapsed / GOAL_FLASH_INTERVAL_MS) % 2) == 0;

  if (showGoal) {
    uint16_t colour = (goalScorerTeam == "Home") ? homeColour : awayColour;
    tft.fillScreen(TFT_BLACK);

    // Big "GOAL!" text
    drawCenteredText("GOAL!!!", 50, colour, 4);

    // Show who scored
    char buf[24];
    sprintf(buf, "%s scores!", goalScorerTeam.c_str());
    drawCenteredText(buf, 110, colour, 2);

    // Show updated score
    char scoreBuf[16];
    sprintf(scoreBuf, "%d : %d", homeScore, awayScore);
    drawCenteredText(scoreBuf, 160, TFT_WHITE, 3);
  } else {
    tft.fillScreen(TFT_BLACK);
  }
}

// ============================================================
// ====================== GOAL LOGIC ==========================
// ============================================================

// Determine which team scores based on which sensor triggered and
// which half it is (teams switch sides at halftime).
void processGoal(bool leftSensor) {
  // First half:  left sensor  = home goal = Away scores
  //              right sensor = away goal = Home scores
  // Second half: sides swapped
  //              left sensor  = away goal = Home scores
  //              right sensor = home goal = Away scores

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

  goalFlashing = true;
  goalFlashStart = millis();
}

void checkGoals() {
  if (gameState != STATE_FIRST_HALF && gameState != STATE_SECOND_HALF) {
    return;
  }

  unsigned long now = millis();

  // --- Left sensor ---
  long leftDist = readDistanceMm(HOME_TRIG_PIN, HOME_ECHO_PIN);

  if (leftDist < GOAL_THRESHOLD_MM) {
    if (!leftSensorTriggered) {
      leftSensorTriggered = true;
      leftSensorBelowSince = now;
    } else if (!leftGoalCounted && (now - leftSensorBelowSince >= GOAL_DETECT_DURATION_MS)) {
      // Ball has been present long enough — count the goal
      leftGoalCounted = true;
      processGoal(true);
    }
  } else {
    leftSensorTriggered = false;
    leftGoalCounted = false;
  }

  // --- Right sensor ---
  long rightDist = readDistanceMm(AWAY_TRIG_PIN, AWAY_ECHO_PIN);

  if (rightDist < GOAL_THRESHOLD_MM) {
    if (!rightSensorTriggered) {
      rightSensorTriggered = true;
      rightSensorBelowSince = now;
    } else if (!rightGoalCounted && (now - rightSensorBelowSince >= GOAL_DETECT_DURATION_MS)) {
      rightGoalCounted = true;
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

    // Wait for release (simple debounce)
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
        // Reset everything
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
        // During play — button does nothing
        break;
    }
  }
}

// ============================================================
// ======================== SETUP =============================
// ============================================================

void setup() {
  Serial.begin(115200);

  // Ultrasonic pins
  pinMode(HOME_TRIG_PIN, OUTPUT);
  pinMode(HOME_ECHO_PIN, INPUT);
  pinMode(AWAY_TRIG_PIN, OUTPUT);
  pinMode(AWAY_ECHO_PIN, INPUT);

  // Button with internal pull-up
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Compute 16-bit colours
  homeColour = tft.color565(HOME_COLOUR_R, HOME_COLOUR_G, HOME_COLOUR_B);
  awayColour = tft.color565(AWAY_COLOUR_R, AWAY_COLOUR_G, AWAY_COLOUR_B);

  // Init display
  tft.init();
  tft.setRotation(1); // Landscape
  tft.fillScreen(TFT_BLACK);

  drawFullScoreboard();
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
      delay(50); // Small delay for animation pacing
      return;    // Skip normal updates during flash
    } else {
      goalFlashing = false;
      forceFullRedraw = true;
    }
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

  delay(50); // ~20Hz update rate
}
