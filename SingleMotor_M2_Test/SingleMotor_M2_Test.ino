#include <Arduino.h>
#include <SCServo.h>

SMS_STS st;

// ========== 接口 ==========
#define RX 44
#define TX 43

#define BUTTON_PIN 46
#define BUTTON_ACTIVE LOW

// ========== 当前测试电机 ==========
#define MOTOR_ID 2
#define MOTOR_NAME "M2"
#define ONE_TURN 4096L

// ========== 运动参数 ==========
const long QUARTER_TURN = ONE_TURN / 4;
const int UP_STEP_COUNT = 4;

// 注意：你之前确认过“target=0 时向上”，说明负速度方向是上行。
// 本程序回零时会先正速度向下到底，然后把最低点作为物理 0 点。
// 之后上升目标使用负数：-1/4圈、-2/4圈、-3/4圈、-4/4圈。
const int HOMING_DOWN_SPEED = 100;
const int HOMING_DOWN_PROBE_SPEED = 150;
const int WHEEL_MAX_SPEED = 650;
const int M2_UP_MIN_SPEED = 180;
const int M2_DOWN_MIN_SPEED = 90;
const int M2_UP_GRAVITY_COMP = 70;
const int M2_SPEED_SLEW_STEP = 45;
const int WHEEL_TOLERANCE = 80;

// ========== 安全参数 ==========
const int CURRENT_LIMIT = 2000;
const int HOMING_CURRENT_LIMIT = 180;
const int HOMING_BOTTOM_CURRENT = 20;
const int HOMING_BOTTOM_LOAD_DELTA = 60;
const int HOMING_BOTTOM_VELOCITY_LIMIT = 35;
const unsigned long HOMING_BOTTOM_CONFIRM_MS = 200;
const unsigned long HOMING_TIMEOUT_MS = 60000;
const unsigned long HOMING_NO_MOVE_TIMEOUT_MS = 2500;
const long HOMING_MIN_TRAVEL = ONE_TURN / 16;
const long HOMING_MAX_TRAVEL = ONE_TURN * 8;
const unsigned long MOVE_TIMEOUT_MS = 15000;
const unsigned long STALL_GRACE_MS = 600;
const unsigned long STALL_CONFIRM_MS = 1500;
const int STALL_VELOCITY_LIMIT = 20;
const long WHEEL_STALL_ERROR_LIMIT = 260;
const unsigned long DWELL_MS = 300;
const unsigned long OSCILLATE_DWELL_MS = 2000;
const unsigned long HOME_HOLD_MS = 600;
const unsigned long DEBUG_INTERVAL_MS = 500;

struct WheelTracker {
  int lastPos;
  long totalPos;
  long targetPos;

  float velocity;
  long lastTotalPos;
  unsigned long lastVelTime;

  int lastCmdSpeed;
};

enum TestState {
  STATE_WAIT_START,
  STATE_HOMING_DOWN,
  STATE_HOME_HOLD,
  STATE_STEP_UP,
  STATE_OSCILLATE,
  STATE_PAUSED,
  STATE_EMERGENCY_STOP
};

WheelTracker motor;

bool workflowStarted = false;
bool running = false;
bool emergencyStop = false;
bool lastButtonState = false;

TestState state = STATE_WAIT_START;
TestState stateBeforePause = STATE_WAIT_START;

int upStepIndex = 0;
bool oscillateGoingDown = true;

unsigned long stateStartTime = 0;
unsigned long stallStartTime = 0;
unsigned long dwellUntil = 0;
unsigned long lastDebugTime = 0;
long homingStartTotal = 0;
int homingStartCurrent = 0;
int homingStartLoad = 0;
unsigned long homingNoMoveStartTime = 0;
unsigned long homingBottomSignalStartTime = 0;
bool homingProbeBoosted = false;

void initMotor();
void handleButtonPress();
void startWorkflow();
void pauseWorkflow();
void resumeWorkflow();
void emergencyStopNow(const char *reason);

void updateWheelMotor();
void resetBottomAsZero();
void runStateMachine();
void enterState(TestState nextState);
void setMoveTarget(long target);

void runHomingDown();
void runHomeHold();
void runStepUp();
void runOscillate();

bool controlWheelToTarget();
bool checkCurrentProtection();
bool checkMoveStallProtection();
bool checkHomingBottomDetected();
bool readButtonDebounced();

void stopMotor();
void printDebug();

long absLong(long value);
float absFloat(float value);
const char* stateName(TestState s);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial1.begin(1000000, SERIAL_8N1, RX, TX);
  st.pSerial = &Serial1;
  delay(1000);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  initMotor();

  Serial.println();
  Serial.println("========== M2 Homing + Step Test ==========");
  Serial.println("Button: start -> pause -> resume.");
  Serial.println("Flow: home down -> zero bottom -> 4 step up -> 1/4 turn oscillation.");
  Serial.println("Protection: current limit, stall detection, timeout.");
  Serial.println("===========================================");
}

void loop() {
  bool nowPressed = readButtonDebounced();
  if (nowPressed && !lastButtonState) {
    handleButtonPress();
  }
  lastButtonState = nowPressed;

  updateWheelMotor();

  if (running && !emergencyStop) {
    if (!checkCurrentProtection()) {
      runStateMachine();
    }
  }

  if (millis() - lastDebugTime >= DEBUG_INTERVAL_MS) {
    lastDebugTime = millis();
    printDebug();
  }

  delay(10);
}

void initMotor() {
  int pos = st.ReadPos(MOTOR_ID);
  if (pos < 0) pos = 0;

  motor.lastPos = pos;
  motor.totalPos = 0;
  motor.targetPos = 0;
  motor.velocity = 0;
  motor.lastTotalPos = 0;
  motor.lastVelTime = millis();
  motor.lastCmdSpeed = 0;
}

void handleButtonPress() {
  if (emergencyStop) {
    Serial.println("Emergency stop is latched. Check hardware and reset ESP32 before continuing.");
    return;
  }

  if (!workflowStarted) {
    startWorkflow();
    return;
  }

  if (state == STATE_PAUSED) {
    resumeWorkflow();
  } else {
    pauseWorkflow();
  }
}

void startWorkflow() {
  workflowStarted = true;
  running = true;
  emergencyStop = false;
  upStepIndex = 0;
  oscillateGoingDown = true;
  dwellUntil = 0;

  Serial.println("Start M2 test. Homing down first.");
  enterState(STATE_HOMING_DOWN);
}

void pauseWorkflow() {
  stateBeforePause = state;
  running = false;
  stopMotor();
  state = STATE_PAUSED;
  Serial.println("Paused.");
}

void resumeWorkflow() {
  running = true;
  enterState(stateBeforePause);
  Serial.println("Resumed.");
}

void emergencyStopNow(const char *reason) {
  emergencyStop = true;
  running = false;
  stopMotor();
  state = STATE_EMERGENCY_STOP;
  Serial.print("EMERGENCY STOP: ");
  Serial.println(reason);
}

void updateWheelMotor() {
  int pos = st.ReadPos(MOTOR_ID);
  if (pos < 0) return;

  int delta = pos - motor.lastPos;
  if (delta > 2048) delta -= 4096;
  if (delta < -2048) delta += 4096;

  motor.totalPos += delta;
  motor.lastPos = pos;

  unsigned long now = millis();
  if (now - motor.lastVelTime >= 20) {
    float dt = (now - motor.lastVelTime) / 1000.0f;
    if (dt > 0) {
      motor.velocity = (motor.totalPos - motor.lastTotalPos) / dt;
      motor.lastTotalPos = motor.totalPos;
      motor.lastVelTime = now;
    }
  }
}

void resetBottomAsZero() {
  int pos = st.ReadPos(MOTOR_ID);
  if (pos >= 0) {
    motor.lastPos = pos;
  }

  motor.totalPos = 0;
  motor.targetPos = 0;
  motor.velocity = 0;
  motor.lastTotalPos = 0;
  motor.lastVelTime = millis();
  motor.lastCmdSpeed = 0;

  Serial.println("Bottom detected. Set lowest point as zero.");
}

void runStateMachine() {
  switch (state) {
    case STATE_WAIT_START:
    case STATE_PAUSED:
    case STATE_EMERGENCY_STOP:
      break;

    case STATE_HOMING_DOWN:
      runHomingDown();
      break;

    case STATE_HOME_HOLD:
      runHomeHold();
      break;

    case STATE_STEP_UP:
      runStepUp();
      break;

    case STATE_OSCILLATE:
      runOscillate();
      break;
  }
}

void enterState(TestState nextState) {
  state = nextState;
  stateStartTime = millis();
  stallStartTime = 0;
  dwellUntil = 0;

  switch (state) {
    case STATE_HOMING_DOWN:
      stopMotor();
      homingStartTotal = motor.totalPos;
      homingStartCurrent = st.ReadCurrent(MOTOR_ID);
      homingStartLoad = st.ReadLoad(MOTOR_ID);
      if (homingStartCurrent < 0) homingStartCurrent = 0;
      if (homingStartLoad < 0) homingStartLoad = 0;
      homingNoMoveStartTime = 0;
      homingBottomSignalStartTime = 0;
      homingProbeBoosted = false;
      Serial.println("STATE: HOMING_DOWN, moving down slowly.");
      break;

    case STATE_HOME_HOLD:
      stopMotor();
      dwellUntil = millis() + HOME_HOLD_MS;
      Serial.println("STATE: HOME_HOLD.");
      break;

    case STATE_STEP_UP:
      upStepIndex++;
      setMoveTarget(-QUARTER_TURN * upStepIndex);
      Serial.print("STATE: STEP_UP ");
      Serial.print(upStepIndex);
      Serial.print("/");
      Serial.print(UP_STEP_COUNT);
      Serial.print(" target=");
      Serial.println(motor.targetPos);
      break;

    case STATE_OSCILLATE:
      oscillateGoingDown = true;
      setMoveTarget(-QUARTER_TURN * (UP_STEP_COUNT - 1));
      Serial.println("STATE: OSCILLATE, moving down 1/4 turn first.");
      break;

    default:
      break;
  }
}

void setMoveTarget(long target) {
  motor.targetPos = target;
  stateStartTime = millis();
  stallStartTime = 0;
}

void runHomingDown() {
  if (millis() - stateStartTime > HOMING_TIMEOUT_MS) {
    emergencyStopNow("homing timeout");
    return;
  }

  if (absLong(motor.totalPos - homingStartTotal) > HOMING_MAX_TRAVEL) {
    emergencyStopNow("homing max travel");
    return;
  }

  int homingSpeed = homingProbeBoosted ? HOMING_DOWN_PROBE_SPEED : HOMING_DOWN_SPEED;
  st.WriteSpe(MOTOR_ID, homingSpeed);
  motor.lastCmdSpeed = homingSpeed;

  if (checkHomingBottomDetected()) {
    stopMotor();
    resetBottomAsZero();
    enterState(STATE_HOME_HOLD);
  }
}

void runHomeHold() {
  if (millis() >= dwellUntil) {
    enterState(STATE_STEP_UP);
  }
}

void runStepUp() {
  if (checkMoveStallProtection()) return;

  if (controlWheelToTarget()) {
    stopMotor();
    Serial.print("Step up reached: ");
    Serial.print(upStepIndex);
    Serial.print("/");
    Serial.println(UP_STEP_COUNT);

    if (upStepIndex >= UP_STEP_COUNT) {
      dwellUntil = millis() + DWELL_MS;
      enterState(STATE_OSCILLATE);
    } else {
      dwellUntil = millis() + DWELL_MS;
      while (millis() < dwellUntil) {
        updateWheelMotor();
        delay(5);
      }
      enterState(STATE_STEP_UP);
    }
  }
}

void runOscillate() {
  if (checkMoveStallProtection()) return;

  if (controlWheelToTarget()) {
    stopMotor();
    dwellUntil = millis() + OSCILLATE_DWELL_MS;

    while (millis() < dwellUntil) {
      updateWheelMotor();
      delay(5);
    }

    if (oscillateGoingDown) {
      setMoveTarget(-QUARTER_TURN * UP_STEP_COUNT);
      oscillateGoingDown = false;
      Serial.print("Oscillate target up=");
      Serial.println(motor.targetPos);
    } else {
      setMoveTarget(-QUARTER_TURN * (UP_STEP_COUNT - 1));
      oscillateGoingDown = true;
      Serial.print("Oscillate target down=");
      Serial.println(motor.targetPos);
    }
  }
}

bool controlWheelToTarget() {
  long error = motor.targetPos - motor.totalPos;

  if (absLong(error) < WHEEL_TOLERANCE && absFloat(motor.velocity) < 70) {
    motor.lastCmdSpeed = 0;
    st.WriteSpe(MOTOR_ID, 0);
    return true;
  }

  float output = 1.0f * error - 0.45f * motor.velocity;
  if (absLong(error) < 500) {
    output *= 0.7f;
  }

  int speed = (int)output;

  if (speed > WHEEL_MAX_SPEED) speed = WHEEL_MAX_SPEED;
  if (speed < -WHEEL_MAX_SPEED) speed = -WHEEL_MAX_SPEED;

  int directionMinSpeed = speed < 0 ? M2_UP_MIN_SPEED : M2_DOWN_MIN_SPEED;
  if (speed > 0 && speed < directionMinSpeed) speed = directionMinSpeed;
  if (speed < 0 && speed > -directionMinSpeed) speed = -directionMinSpeed;

  if (speed < 0) {
    speed -= M2_UP_GRAVITY_COMP;
    if (speed < -WHEEL_MAX_SPEED) speed = -WHEEL_MAX_SPEED;
  }

  int deltaSpeed = speed - motor.lastCmdSpeed;
  if (deltaSpeed > M2_SPEED_SLEW_STEP) {
    speed = motor.lastCmdSpeed + M2_SPEED_SLEW_STEP;
  } else if (deltaSpeed < -M2_SPEED_SLEW_STEP) {
    speed = motor.lastCmdSpeed - M2_SPEED_SLEW_STEP;
  }

  st.WriteSpe(MOTOR_ID, speed);
  motor.lastCmdSpeed = speed;
  return false;
}

bool checkCurrentProtection() {
  int current = st.ReadCurrent(MOTOR_ID);

  int limit = state == STATE_HOMING_DOWN ? HOMING_CURRENT_LIMIT : CURRENT_LIMIT;

  if (current != -1 && abs(current) > limit) {
    Serial.print("Over current! current=");
    Serial.println(current);
    Serial.print("limit=");
    Serial.println(limit);
    emergencyStopNow(state == STATE_HOMING_DOWN ? "homing over current" : "over current");
    return true;
  }

  return false;
}

bool checkMoveStallProtection() {
  unsigned long now = millis();

  if (now - stateStartTime > MOVE_TIMEOUT_MS) {
    emergencyStopNow("move timeout");
    return true;
  }

  if (now - stateStartTime < STALL_GRACE_MS) {
    return false;
  }

  long error = motor.targetPos - motor.totalPos;

  if (absLong(error) <= WHEEL_TOLERANCE * 2 || absLong(error) <= WHEEL_STALL_ERROR_LIMIT) {
    stallStartTime = 0;
    return false;
  }

  bool lowVelocity = absFloat(motor.velocity) < STALL_VELOCITY_LIMIT;
  bool commandingMove = abs(motor.lastCmdSpeed) >= M2_DOWN_MIN_SPEED;

  if (lowVelocity && commandingMove) {
    if (stallStartTime == 0) stallStartTime = now;
    if (now - stallStartTime > STALL_CONFIRM_MS) {
      emergencyStopNow("wheel stall");
      return true;
    }
  } else {
    stallStartTime = 0;
  }

  return false;
}

bool checkHomingBottomDetected() {
  unsigned long now = millis();

  if (now - stateStartTime < STALL_GRACE_MS) {
    return false;
  }

  long homingTravel = absLong(motor.totalPos - homingStartTotal);
  int current = st.ReadCurrent(MOTOR_ID);
  int load = st.ReadLoad(MOTOR_ID);
  int currentRise = current < 0 ? 0 : abs(current) - abs(homingStartCurrent);
  int loadRise = load < 0 ? 0 : abs(load) - abs(homingStartLoad);
  bool bottomSignal = false;
  bool stoppedAtBottom = absFloat(motor.velocity) < HOMING_BOTTOM_VELOCITY_LIMIT;

  if (current != -1 && abs(current) >= HOMING_BOTTOM_CURRENT) {
    bottomSignal = true;
  }

  if (load != -1 && loadRise >= HOMING_BOTTOM_LOAD_DELTA) {
    bottomSignal = true;
  }

  if (homingTravel < HOMING_MIN_TRAVEL) {
    if (bottomSignal && stoppedAtBottom) {
      if (homingBottomSignalStartTime == 0) {
        homingBottomSignalStartTime = now;
      }

      if (now - homingBottomSignalStartTime >= HOMING_BOTTOM_CONFIRM_MS) {
        Serial.print("Homing bottom near start. travel=");
        Serial.print(homingTravel);
        Serial.print(" velocity=");
        Serial.print(motor.velocity);
        Serial.print(" current=");
        Serial.print(current);
        Serial.print(" currentRise=");
        Serial.print(currentRise);
        Serial.print(" load=");
        Serial.print(load);
        Serial.print(" loadRise=");
        Serial.println(loadRise);
        return true;
      }
    } else {
      homingBottomSignalStartTime = 0;
    }

    if (absFloat(motor.velocity) < STALL_VELOCITY_LIMIT) {
      if (homingNoMoveStartTime == 0) {
        homingNoMoveStartTime = now;
      }

      if (now - homingNoMoveStartTime > HOMING_NO_MOVE_TIMEOUT_MS) {
        if (!homingProbeBoosted) {
          homingProbeBoosted = true;
          homingNoMoveStartTime = now;
          Serial.println("Homing no movement, probing with higher down speed.");
        } else {
          if (bottomSignal) {
            Serial.print("No movement after probe with bottom signal. travel=");
            Serial.print(homingTravel);
            Serial.print(" current=");
            Serial.print(current);
            Serial.print(" currentRise=");
            Serial.print(currentRise);
            Serial.print(" load=");
            Serial.print(load);
            Serial.print(" loadRise=");
            Serial.println(loadRise);
            return true;
          }

          emergencyStopNow("homing no movement before bottom signal");
          return false;
        }
      }
    } else {
      homingNoMoveStartTime = 0;
    }

    return false;
  }

  homingNoMoveStartTime = 0;

  if (bottomSignal && stoppedAtBottom) {
    if (homingBottomSignalStartTime == 0) {
      homingBottomSignalStartTime = now;
    }

    if (now - homingBottomSignalStartTime >= HOMING_BOTTOM_CONFIRM_MS) {
      Serial.print("Homing bottom signal. travel=");
      Serial.print(homingTravel);
      Serial.print(" velocity=");
      Serial.print(motor.velocity);
      Serial.print(" current=");
      Serial.print(current);
      Serial.print(" currentRise=");
      Serial.print(currentRise);
      Serial.print(" load=");
      Serial.print(load);
      Serial.print(" loadRise=");
      Serial.println(loadRise);
      return true;
    }
  } else {
    homingBottomSignalStartTime = 0;
  }

  if (absFloat(motor.velocity) < STALL_VELOCITY_LIMIT) {
    if (stallStartTime == 0) stallStartTime = now;
    if (now - stallStartTime > STALL_CONFIRM_MS) {
      Serial.print("Homing bottom by stall. travel=");
      Serial.print(homingTravel);
      Serial.print(" velocity=");
      Serial.print(motor.velocity);
      Serial.print(" current=");
      Serial.print(current);
      Serial.print(" currentRise=");
      Serial.print(currentRise);
      Serial.print(" load=");
      Serial.print(load);
      Serial.print(" loadRise=");
      Serial.println(loadRise);
      return true;
    }
  } else {
    stallStartTime = 0;
  }

  return false;
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

void stopMotor() {
  st.WriteSpe(MOTOR_ID, 0);
  motor.lastCmdSpeed = 0;
}

void printDebug() {
  int pos = st.ReadPos(MOTOR_ID);
  int speed = st.ReadSpeed(MOTOR_ID);
  int current = st.ReadCurrent(MOTOR_ID);
  int load = st.ReadLoad(MOTOR_ID);
  int voltage = st.ReadVoltage(MOTOR_ID);
  int temper = st.ReadTemper(MOTOR_ID);

  long logicalHeight = -motor.totalPos;
  long logicalTarget = -motor.targetPos;

  Serial.print("[");
  Serial.print(MOTOR_NAME);
  Serial.print("] state=");
  Serial.print(stateName(state));
  Serial.print(" run=");
  Serial.print(running);
  Serial.print(" emg=");
  Serial.print(emergencyStop);
  Serial.print(" pos=");
  Serial.print(pos);
  Serial.print(" physicalTotal=");
  Serial.print(motor.totalPos);
  Serial.print(" height=");
  Serial.print(logicalHeight);
  Serial.print(" targetHeight=");
  Serial.print(logicalTarget);
  Serial.print(" err=");
  Serial.print(motor.targetPos - motor.totalPos);
  Serial.print(" vel=");
  Serial.print(motor.velocity);
  Serial.print(" cmd=");
  Serial.print(motor.lastCmdSpeed);
  Serial.print(" speed=");
  Serial.print(speed);
  Serial.print(" current=");
  Serial.print(current);
  Serial.print(" load=");
  Serial.print(load);
  Serial.print(" voltage=");
  Serial.print(voltage);
  Serial.print(" temp=");
  Serial.println(temper);
}

long absLong(long value) {
  return value < 0 ? -value : value;
}

float absFloat(float value) {
  return value < 0 ? -value : value;
}

const char* stateName(TestState s) {
  switch (s) {
    case STATE_WAIT_START: return "WAIT_START";
    case STATE_HOMING_DOWN: return "HOMING_DOWN";
    case STATE_HOME_HOLD: return "HOME_HOLD";
    case STATE_STEP_UP: return "STEP_UP";
    case STATE_OSCILLATE: return "OSCILLATE";
    case STATE_PAUSED: return "PAUSED";
    case STATE_EMERGENCY_STOP: return "EMERGENCY_STOP";
    default: return "UNKNOWN";
  }
}
