#include <Arduino.h>
#include <SCServo.h>

SCSCL sc;

// ============================================================
// M5 SC09 fixed-position test
// ============================================================
// Hardware:
//   SC09 TX -> ESP32 RX44
//   SC09 RX -> ESP32 TX43
//   Button  -> GPIO46 to GND, using INPUT_PULLUP
//
// Behavior:
//   Press button once.
//   Move M5 to fixed hold position 820.
//   Move to position 700.
//   Move back to fixed hold position 820.
//
// Notes:
//   This sketch does not capture a center position.
//   This sketch does not write servo EEPROM.
//   This sketch does not save any center value to ESP32 flash.
// ============================================================

#define RX 44
#define TX 43
#define BUTTON_PIN 46
#define BUTTON_ACTIVE LOW
#define M5_ID 5

const int SC09_MIN_POS = 0;
const int SC09_MAX_POS = 1023;

// SC09 range is 300 degrees / 1024 steps.
// This test uses fixed absolute positions instead of a degree offset.
const int M5_HOLD_POS = 800;
const int M5_TARGET_POS = 700;

const int POSITION_SPEED = 600;
const int POSITION_TIME = 0;
const int POSITION_TOLERANCE = 4;

const int M5_CURRENT_LIMIT = 350;
const unsigned long CURRENT_CONFIRM_MS = 200;

const unsigned long SEQUENCE_DWELL_MS = 1000;
const unsigned long SEQUENCE_MOVE_TIMEOUT_MS = 8000;
const unsigned long DEBUG_INTERVAL_MS = 500;

int targetPos = M5_HOLD_POS;
bool lastButtonState = false;
bool sequenceRunning = false;
int sequenceStep = 0;
unsigned long sequenceStepStartTime = 0;
unsigned long sequenceDwellUntil = 0;
unsigned long lastDebugTime = 0;
unsigned long currentOverStartTime = 0;

void holdCurrentPositionOnStartup();
void handleButtonPress();
void startTestSequence();
void runTestSequence();
void advanceSequence();
void moveToTarget(int pos, const char *label);
bool readButtonDebounced();
bool isAtTarget();
void stopAndHoldCurrent();
bool checkCurrentProtection();
void printDebug();
bool readFeedback(int &pos, int &speed, int &current, int &load, int &voltage, int &temper, int &move);
int clampPosition(int pos);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial1.begin(1000000, SERIAL_8N1, RX, TX);
  sc.pSerial = &Serial1;
  delay(1000);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  holdCurrentPositionOnStartup();

  Serial.println();
  Serial.println("========== M5 SC09 Fixed 820 -> 700 -> 820 Test ==========");
  Serial.println("M5 model: SC09, SCSCL protocol, range 0~1023, 300deg/1024.");
  Serial.println("Button only.");
  Serial.println("Button flow: 820 -> 700 -> 820.");
  Serial.println("No center capture. No EEPROM write. No ESP32 flash save.");
  Serial.println("Serial input is ignored; serial monitor is only for debug output.");
  Serial.println("========================================================");
}

void loop() {
  bool nowPressed = readButtonDebounced();
  if (nowPressed && !lastButtonState) {
    handleButtonPress();
  }
  lastButtonState = nowPressed;

  if (checkCurrentProtection()) {
    sequenceRunning = false;
    stopAndHoldCurrent();
  }

  runTestSequence();

  if (millis() - lastDebugTime >= DEBUG_INTERVAL_MS) {
    lastDebugTime = millis();
    printDebug();
  }

  delay(10);
}

void holdCurrentPositionOnStartup() {
  int pos = sc.ReadPos(M5_ID);
  if (pos < 0) {
    Serial.println("Startup hold failed: cannot read M5 SC09 position.");
    return;
  }

  sequenceRunning = false;
  moveToTarget(pos, "STARTUP_HOLD_CURRENT");
}

void handleButtonPress() {
  if (sequenceRunning) {
    Serial.println("M5 sequence is already running. Button ignored.");
    return;
  }

  Serial.println("Button pressed: start fixed 820 -> 700 -> 820 test.");
  startTestSequence();
}

void startTestSequence() {
  sequenceRunning = true;
  sequenceStep = 0;
  sequenceDwellUntil = 0;
  currentOverStartTime = 0;

  Serial.print("Start M5 SC09 test: ");
  Serial.print(M5_HOLD_POS);
  Serial.print(" -> ");
  Serial.print(M5_TARGET_POS);
  Serial.print(" -> ");
  Serial.println(M5_HOLD_POS);

  advanceSequence();
}

void runTestSequence() {
  if (!sequenceRunning) return;

  unsigned long now = millis();

  if (sequenceDwellUntil != 0) {
    if (now >= sequenceDwellUntil) {
      sequenceDwellUntil = 0;
      advanceSequence();
    }
    return;
  }

  if (isAtTarget()) {
    sequenceDwellUntil = now + SEQUENCE_DWELL_MS;
    return;
  }

  if (now - sequenceStepStartTime > SEQUENCE_MOVE_TIMEOUT_MS) {
    sequenceRunning = false;
    stopAndHoldCurrent();
    Serial.println("M5 SC09 test sequence timeout. Hold current position.");
  }
}

void advanceSequence() {
  sequenceStep++;
  sequenceStepStartTime = millis();

  switch (sequenceStep) {
    case 1:
      moveToTarget(M5_HOLD_POS, "STEP1_GO_HOLD_820");
      break;

    case 2:
      moveToTarget(M5_TARGET_POS, "STEP2_GO_TARGET_700");
      break;

    case 3:
      moveToTarget(M5_HOLD_POS, "STEP3_BACK_HOLD_820");
      break;

    default:
      sequenceRunning = false;
      Serial.println("M5 SC09 test sequence done.");
      break;
  }
}

void moveToTarget(int pos, const char *label) {
  targetPos = clampPosition(pos);
  sc.WritePos(M5_ID, targetPos, POSITION_TIME, POSITION_SPEED);

  Serial.print("M5 target ");
  Serial.print(label);
  Serial.print(" = ");
  Serial.println(targetPos);
}

bool readButtonDebounced() {
  static bool stableState = false;
  static bool lastRawState = false;
  static unsigned long lastChangeTime = 0;

  bool rawState = (digitalRead(BUTTON_PIN) == BUTTON_ACTIVE);

  if (rawState != lastRawState) {
    lastRawState = rawState;
    lastChangeTime = millis();
  }

  if (millis() - lastChangeTime >= 30) {
    stableState = rawState;
  }

  return stableState;
}

bool isAtTarget() {
  int pos = sc.ReadPos(M5_ID);
  if (pos < 0) return false;
  return abs(targetPos - pos) <= POSITION_TOLERANCE;
}

void stopAndHoldCurrent() {
  int pos = sc.ReadPos(M5_ID);
  if (pos >= 0) {
    moveToTarget(pos, "HOLD_CURRENT");
  }
}

bool checkCurrentProtection() {
  int current = sc.ReadCurrent(M5_ID);
  if (current < 0) {
    currentOverStartTime = 0;
    return false;
  }

  if (abs(current) <= M5_CURRENT_LIMIT) {
    currentOverStartTime = 0;
    return false;
  }

  unsigned long now = millis();
  if (currentOverStartTime == 0) {
    currentOverStartTime = now;
  }

  if (now - currentOverStartTime >= CURRENT_CONFIRM_MS) {
    Serial.print("M5 CURRENT LIMIT! current=");
    Serial.print(current);
    Serial.print(" limit=");
    Serial.println(M5_CURRENT_LIMIT);
    currentOverStartTime = 0;
    return true;
  }

  return false;
}

void printDebug() {
  int pos = -1;
  int speed = -1;
  int current = -1;
  int load = -1;
  int voltage = -1;
  int temper = -1;
  int move = -1;
  bool feedbackOk = readFeedback(pos, speed, current, load, voltage, temper, move);
  bool atTarget = feedbackOk && abs(targetPos - pos) <= POSITION_TOLERANCE;

  Serial.print("[M5-SC09] hold=820 targetPoint=700 target=");
  Serial.print(targetPos);
  Serial.print(" pos=");
  Serial.print(pos);
  Serial.print(" err=");
  Serial.print(feedbackOk ? targetPos - pos : 0);
  Serial.print(" atTarget=");
  Serial.print(atTarget);
  Serial.print(" step=");
  Serial.print(sequenceStep);
  Serial.print(" running=");
  Serial.print(sequenceRunning);
  Serial.print(" speed=");
  Serial.print(speed);
  Serial.print(" current=");
  Serial.print(current);
  Serial.print(" load=");
  Serial.print(load);
  Serial.print(" voltage=");
  Serial.print(voltage);
  Serial.print(" temp=");
  Serial.print(temper);
  Serial.print(" move=");
  Serial.print(move);
  Serial.print(" feedbackOk=");
  Serial.println(feedbackOk);
}

bool readFeedback(int &pos, int &speed, int &current, int &load, int &voltage, int &temper, int &move) {
  if (sc.FeedBack(M5_ID) == -1) {
    return false;
  }

  pos = sc.ReadPos(-1);
  speed = sc.ReadSpeed(-1);
  current = sc.ReadCurrent(-1);
  load = sc.ReadLoad(-1);
  voltage = sc.ReadVoltage(-1);
  temper = sc.ReadTemper(-1);
  move = sc.ReadMove(-1);
  return pos >= 0;
}

int clampPosition(int pos) {
  if (pos < SC09_MIN_POS) return SC09_MIN_POS;
  if (pos > SC09_MAX_POS) return SC09_MAX_POS;
  return pos;
}
