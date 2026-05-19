#include <Arduino.h>
#include <SCServo.h>

SMS_STS st;

// ========== 接口 ==========
#define RX 44
#define TX 43

#define BUTTON_PIN 46
#define BUTTON_ACTIVE LOW

// ========== 当前测试电机 ==========
#define MOTOR_ID 3
#define MOTOR_NAME "M3"
#define USE_POSITION_MODE false

#define ONE_TURN 4096L

// 单电机安全测试：只在起点和起点附近 1/4 圈之间来回。
// 如果想加大观察幅度，可以改成 ONE_TURN / 3 或 ONE_TURN / 2，但建议先从 1/4 圈开始。
const long TEST_AMPLITUDE = ONE_TURN / 4;

// ========== 安全参数 ==========
const int CURRENT_LIMIT = 2000;
const unsigned long MOVE_TIMEOUT_MS = 12000;
const unsigned long STALL_GRACE_MS = 500;
const unsigned long STALL_CONFIRM_MS = 1500;
const int STALL_VELOCITY_LIMIT = 20;
const long WHEEL_STALL_ERROR_LIMIT = 220;
const int POSITION_STALL_DELTA_LIMIT = 2;

// ========== 控制参数 ==========
const int WHEEL_MAX_SPEED = 600;
const int WHEEL_MIN_SPEED = 120;
const int POSITION_SPEED = 500;
const int POSITION_ACC = 30;
const int WHEEL_TOLERANCE = 60;
const int POSITION_TOLERANCE = 8;
const unsigned long DWELL_MS = 400;
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

WheelTracker motor;

bool workflowStarted = false;
bool running = false;
bool paused = false;
bool emergencyStop = false;
bool lastButtonState = false;

bool movingAway = true;
unsigned long moveStartTime = 0;
unsigned long stallStartTime = 0;
unsigned long dwellUntil = 0;
unsigned long lastDebugTime = 0;

long wheelHomeTotal = 0;

int positionHome = 0;
int positionAway = 0;
int positionTarget = 0;
int positionStallReference = 0;
int pausedPositionTarget = 0;

void initMotor();
void handleButtonPress();
void startWorkflow();
void pauseWorkflow();
void resumeWorkflow();
void emergencyStopNow(const char *reason);

void updateWheelMotor();
void setNextTarget();
void runMotion();

bool controlWheelToTarget();
bool controlPositionToTarget();

bool checkCurrentProtection();
bool checkStallProtection();
bool readButtonDebounced();

void stopMotor();
void printDebug();

int wrapPos(int pos);
int circularError(int target, int current);
long absLong(long value);
float absFloat(float value);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial1.begin(1000000, SERIAL_8N1, RX, TX);
  st.pSerial = &Serial1;
  delay(1000);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  initMotor();

  Serial.println();
  Serial.println("========== Single Motor Test ==========");
  Serial.print("Motor: ");
  Serial.print(MOTOR_NAME);
  Serial.print(" | ID=");
  Serial.println(MOTOR_ID);
  Serial.print("Mode: ");
  Serial.println(USE_POSITION_MODE ? "POSITION" : "WHEEL");
  Serial.println("Button: start -> pause -> resume.");
  Serial.println("Emergency stop on over-current, stall, or move timeout.");
  Serial.println("=======================================");
}

void loop() {
  bool nowPressed = readButtonDebounced();
  if (nowPressed && !lastButtonState) {
    handleButtonPress();
  }
  lastButtonState = nowPressed;

  if (!USE_POSITION_MODE) {
    updateWheelMotor();
  }

  if (running && !paused && !emergencyStop) {
    if (checkCurrentProtection() || checkStallProtection()) {
      stopMotor();
    } else {
      runMotion();
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

  wheelHomeTotal = motor.totalPos;

  positionHome = pos;
  if (positionHome + TEST_AMPLITUDE <= 4095) {
    positionAway = positionHome + TEST_AMPLITUDE;
  } else {
    positionAway = positionHome - TEST_AMPLITUDE;
  }
  positionTarget = positionHome;
  pausedPositionTarget = positionHome;
  positionStallReference = positionHome;
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

  if (paused) {
    resumeWorkflow();
  } else {
    pauseWorkflow();
  }
}

void startWorkflow() {
  workflowStarted = true;
  running = true;
  paused = false;
  emergencyStop = false;
  movingAway = true;
  dwellUntil = 0;

  Serial.print("Start ");
  Serial.println(MOTOR_NAME);

  setNextTarget();
}

void pauseWorkflow() {
  paused = true;
  running = false;
  pausedPositionTarget = positionTarget;
  stopMotor();
  Serial.println("Paused.");
}

void resumeWorkflow() {
  paused = false;
  running = true;
  positionTarget = pausedPositionTarget;
  moveStartTime = millis();
  stallStartTime = 0;
  positionStallReference = st.ReadPos(MOTOR_ID);
  Serial.println("Resumed.");
}

void emergencyStopNow(const char *reason) {
  emergencyStop = true;
  running = false;
  paused = false;
  stopMotor();
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

void setNextTarget() {
  moveStartTime = millis();
  stallStartTime = 0;

  if (USE_POSITION_MODE) {
    positionTarget = movingAway ? positionAway : positionHome;
    positionStallReference = st.ReadPos(MOTOR_ID);
    if (positionStallReference < 0) positionStallReference = positionHome;

    Serial.print("Target ");
    Serial.print(MOTOR_NAME);
    Serial.print(" position=");
    Serial.println(positionTarget);
  } else {
    motor.targetPos = movingAway ? wheelHomeTotal + TEST_AMPLITUDE : wheelHomeTotal;

    Serial.print("Target ");
    Serial.print(MOTOR_NAME);
    Serial.print(" total=");
    Serial.println(motor.targetPos);
  }
}

void runMotion() {
  if (dwellUntil > 0) {
    if (millis() >= dwellUntil) {
      dwellUntil = 0;
      movingAway = !movingAway;
      setNextTarget();
    }
    return;
  }

  bool done = USE_POSITION_MODE ? controlPositionToTarget() : controlWheelToTarget();
  if (done) {
    stopMotor();
    Serial.print(MOTOR_NAME);
    Serial.println(" reached target.");
    dwellUntil = millis() + DWELL_MS;
  }
}

bool controlWheelToTarget() {
  long error = motor.targetPos - motor.totalPos;

  if (absLong(error) < WHEEL_TOLERANCE && absFloat(motor.velocity) < 60) {
    motor.lastCmdSpeed = 0;
    st.WriteSpe(MOTOR_ID, 0);
    return true;
  }

  float output = 1.2f * error - 1.0f * motor.velocity;
  if (absLong(error) < 500) {
    output *= 0.7f;
  }

  int speed = (int)output;
  if (speed > WHEEL_MAX_SPEED) speed = WHEEL_MAX_SPEED;
  if (speed < -WHEEL_MAX_SPEED) speed = -WHEEL_MAX_SPEED;
  if (speed > 0 && speed < WHEEL_MIN_SPEED) speed = WHEEL_MIN_SPEED;
  if (speed < 0 && speed > -WHEEL_MIN_SPEED) speed = -WHEEL_MIN_SPEED;

  st.WriteSpe(MOTOR_ID, speed);
  motor.lastCmdSpeed = speed;
  return false;
}

bool controlPositionToTarget() {
  int posNow = st.ReadPos(MOTOR_ID);
  if (posNow < 0) return false;

  int error = circularError(positionTarget, posNow);
  if (abs(error) < POSITION_TOLERANCE) {
    return true;
  }

  st.WritePosEx(MOTOR_ID, positionTarget, POSITION_SPEED, POSITION_ACC);
  return false;
}

bool checkCurrentProtection() {
  int current = st.ReadCurrent(MOTOR_ID);
  if (current != -1 && abs(current) > CURRENT_LIMIT) {
    Serial.print("Over current! current=");
    Serial.println(current);
    emergencyStopNow("over current");
    return true;
  }

  return false;
}

bool checkStallProtection() {
  if (moveStartTime == 0 || dwellUntil > 0) return false;

  unsigned long now = millis();
  if (now - moveStartTime > MOVE_TIMEOUT_MS) {
    emergencyStopNow("move timeout");
    return true;
  }

  if (now - moveStartTime < STALL_GRACE_MS) {
    return false;
  }

  if (USE_POSITION_MODE) {
    int posNow = st.ReadPos(MOTOR_ID);
    if (posNow < 0) return false;

    int error = circularError(positionTarget, posNow);
    if (abs(error) < POSITION_TOLERANCE * 2) {
      stallStartTime = 0;
      positionStallReference = posNow;
      return false;
    }

    if (abs(circularError(posNow, positionStallReference)) <= POSITION_STALL_DELTA_LIMIT) {
      if (stallStartTime == 0) stallStartTime = now;
      if (now - stallStartTime > STALL_CONFIRM_MS) {
        emergencyStopNow("position stall");
        return true;
      }
    } else {
      stallStartTime = 0;
      positionStallReference = posNow;
    }

    return false;
  }

  long error = motor.targetPos - motor.totalPos;
  if (absLong(error) <= WHEEL_TOLERANCE * 2) {
    stallStartTime = 0;
    return false;
  }

  if (absLong(error) <= WHEEL_STALL_ERROR_LIMIT) {
    stallStartTime = 0;
    return false;
  }

  bool lowVelocity = absFloat(motor.velocity) < STALL_VELOCITY_LIMIT;
  bool commandingMove = abs(motor.lastCmdSpeed) >= WHEEL_MIN_SPEED;

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
  if (USE_POSITION_MODE) {
    int pos = st.ReadPos(MOTOR_ID);
    if (pos >= 0) {
      st.WritePosEx(MOTOR_ID, pos, 0, POSITION_ACC);
    }
  } else {
    st.WriteSpe(MOTOR_ID, 0);
    motor.lastCmdSpeed = 0;
  }
}

void printDebug() {
  int pos = st.ReadPos(MOTOR_ID);
  int speed = st.ReadSpeed(MOTOR_ID);
  int current = st.ReadCurrent(MOTOR_ID);
  int load = st.ReadLoad(MOTOR_ID);
  int voltage = st.ReadVoltage(MOTOR_ID);
  int temper = st.ReadTemper(MOTOR_ID);

  Serial.print("[");
  Serial.print(MOTOR_NAME);
  Serial.print("] run=");
  Serial.print(running);
  Serial.print(" paused=");
  Serial.print(paused);
  Serial.print(" emg=");
  Serial.print(emergencyStop);
  Serial.print(" pos=");
  Serial.print(pos);

  if (USE_POSITION_MODE) {
    Serial.print(" target=");
    Serial.print(positionTarget);
    Serial.print(" err=");
    Serial.print(pos >= 0 ? circularError(positionTarget, pos) : 0);
  } else {
    Serial.print(" total=");
    Serial.print(motor.totalPos);
    Serial.print(" target=");
    Serial.print(motor.targetPos);
    Serial.print(" err=");
    Serial.print(motor.targetPos - motor.totalPos);
    Serial.print(" vel=");
    Serial.print(motor.velocity);
    Serial.print(" cmd=");
    Serial.print(motor.lastCmdSpeed);
  }

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

int wrapPos(int pos) {
  while (pos < 0) pos += 4096;
  while (pos > 4095) pos -= 4096;
  return pos;
}

int circularError(int target, int current) {
  int error = target - current;
  if (error > 2048) error -= 4096;
  if (error < -2048) error += 4096;
  return error;
}

long absLong(long value) {
  return value < 0 ? -value : value;
}

float absFloat(float value) {
  return value < 0 ? -value : value;
}



