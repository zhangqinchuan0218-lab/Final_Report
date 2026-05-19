#include <Arduino.h>
#include <SCServo.h>

SMS_STS st;

// ========== 接口 ==========
#define RX 44
#define TX 43

#define BUTTON_PIN 46
#define PRESSURE_PIN 45

#define BUTTON_ACTIVE LOW
#define PRESSURE_ACTIVE LOW

// ========== 电机ID ==========
#define M1_ID 1
#define M2_ID 2
#define M3_ID 3
#define M4_ID 4

#define ONE_TURN 4096L

// ========== 运行参数 ==========
const long M2_STEP = ONE_TURN / 20;      // 每个动作前 M2 走 1/20 圈

// 协同测试版：
// 正式逻辑中 M2_TOTAL = ONE_TURN * 3，也就是 60 次动作才触发一次 M1。
// 测试电机配合时等待太久，所以这里改成少量动作后就触发 M1。
const int TEST_M2_ACTIONS_BEFORE_M1 = 3;
const long M2_TOTAL = M2_STEP * TEST_M2_ACTIONS_BEFORE_M1;

const long M1_STEP = -ONE_TURN / 4;      // M1 每轮逆时针 1/4 圈
const long M1_TOTAL = -ONE_TURN * 7;     // M1 总共逆时针 7 圈

const long M3_RETRACT_DELTA = -ONE_TURN / 10;

// M4 以初始点为中心，先左 1/4 圈，再右 1/4 圈，最后回中。
// 这等价于从左端到右端共扫过 1/2 圈。
const int M4_LEFT_DELTA = -ONE_TURN / 4;
const int M4_RIGHT_DELTA = ONE_TURN / 4;

// ========== 控制参数 ==========
int wheelMaxSpeed = 1500;
int wheelMinSpeed = 120;
int tolerance = 25;

int m3ApproachSpeed = 250;

int CURRENT_LIMIT = 2000;

bool ENABLE_CURRENT_PROTECTION = true;
bool ENABLE_STALL_PROTECTION = true;
bool ENABLE_CMD_PRINT = true;

const unsigned long M1_MOVE_TIMEOUT_MS = 8000;
const unsigned long M2_MOVE_TIMEOUT_MS = 3000;
const unsigned long STALL_GRACE_MS = 500;
const unsigned long STALL_CONFIRM_MS = 800;
const int STALL_VELOCITY_LIMIT = 25;

// ========== 电机结构 ==========
enum MotorMode {
  WHEEL_MODE,
  POSITION_MODE
};

struct Motor {
  int id;
  MotorMode mode;

  int lastPos;
  long totalPos;
  long targetPos;

  float velocity;
  long lastTotalPos;
  unsigned long lastVelTime;

  int homePos;
  int targetAbsPos;

  bool done;

  float Kp;
  float Kv;

  int lastCmdSpeed;

  unsigned long motionStartTime;
  unsigned long stallStartTime;
};

Motor m1, m2, m3, m4;

// ========== 动作状态 ==========
enum ActionState {
  ACTION_IDLE,
  ACTION_M2_STEP,
  ACTION_M3_APPROACH,
  ACTION_M3_RETRACT,
  ACTION_M4_LEFT,
  ACTION_M4_RIGHT,
  ACTION_M4_HOME,
  ACTION_M3_HOME,
  ACTION_DONE
};

struct CutAction {
  ActionState state;
  bool active;
  bool done;

  long m3HomeTotal;
  long m3TouchPos;
};

CutAction action;

// ========== 系统状态 ==========
enum SystemState {
  SYS_WAIT_START,
  SYS_RUN_ACTION,
  SYS_M1_STEP,
  SYS_PAUSED,
  SYS_FINISHED,
  SYS_EMERGENCY_STOP
};

volatile bool pressureTriggered = false;

bool systemRunning = false;
bool emergencyStop = false;
bool workflowStarted = false;
bool lastButtonState = false;

SystemState systemState = SYS_WAIT_START;
SystemState stateBeforePause = SYS_WAIT_START;
int pausedM4Target = 0;

long m1Progress = 0;
long m2Progress = 0;

// ========== 任务句柄 ==========
TaskHandle_t taskButtonHandle = NULL;
TaskHandle_t taskSensorHandle = NULL;
TaskHandle_t taskMotorHandle = NULL;
TaskHandle_t taskDebugHandle = NULL;

// ========== 函数声明 ==========
void taskButton(void *pvParameters);
void taskSensor(void *pvParameters);
void taskMotor(void *pvParameters);
void taskDebug(void *pvParameters);

void initMotor(Motor &m, int id, MotorMode mode, float kp = 1.2, float kv = 1.0);
void updateWheelMotor(Motor &m);
bool controlWheelToTarget(Motor &m);
bool controlPositionToTarget(Motor &m);

void setWheelTargetRelative(Motor &m, long delta);
void setPositionTargetRelativeFromHome(Motor &m, int delta);

void beginWorkflow();
void pauseSystem();
void resumeSystem();
void handleButtonPress();
bool isM4ActionState(ActionState s);

void startCutAction();
void runCutAction();
void finishCutAction();
void workflowStep();

bool readButtonDebounced();
bool readPressureDebounced();

void stopAllMotors();
void stopWheelMotor(Motor &m);
bool checkCurrentProtection();
bool checkStallProtection();
bool checkWheelMotorStall(Motor &m, const char *name, unsigned long timeoutMs);

void printServoStatus(int id);

int wrapPos(int pos);
const char* systemStateName(SystemState s);
const char* actionStateName(ActionState s);

// ========== setup ==========
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial1.begin(1000000, SERIAL_8N1, RX, TX);
  st.pSerial = &Serial1;
  delay(1000);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(PRESSURE_PIN, INPUT_PULLUP);

  Serial.println("Checking servo communication...");
  for (int id = 1; id <= 4; id++) {
    int pos = st.ReadPos(id);
    Serial.print("ID ");
    Serial.print(id);
    Serial.print(" Pos = ");
    Serial.println(pos);
  }

  initMotor(m1, M1_ID, WHEEL_MODE, 1.2, 1.0);
  initMotor(m2, M2_ID, WHEEL_MODE, 1.2, 1.0);
  initMotor(m3, M3_ID, WHEEL_MODE, 1.0, 0.8);
  initMotor(m4, M4_ID, POSITION_MODE);

  action.state = ACTION_IDLE;
  action.active = false;
  action.done = false;
  action.m3HomeTotal = m3.totalPos;
  action.m3TouchPos = 0;

  Serial.println("M1-M4 action workflow ready.");
  Serial.print("Coordination test: M1 moves after ");
  Serial.print(TEST_M2_ACTIONS_BEFORE_M1);
  Serial.println(" cut actions.");
  Serial.println("Button: first press start, next press pause, next press resume.");

  xTaskCreatePinnedToCore(taskButton, "ButtonTask", 2048, NULL, 3, &taskButtonHandle, 0);
  xTaskCreatePinnedToCore(taskSensor, "SensorTask", 2048, NULL, 3, &taskSensorHandle, 0);
  xTaskCreatePinnedToCore(taskMotor, "MotorTask", 8192, NULL, 4, &taskMotorHandle, 1);
  xTaskCreatePinnedToCore(taskDebug, "DebugTask", 4096, NULL, 1, &taskDebugHandle, 1);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}

// ========== 按钮任务 ==========
void taskButton(void *pvParameters) {
  while (1) {
    bool nowPressed = readButtonDebounced();

    if (nowPressed && !lastButtonState) {
      handleButtonPress();
    }

    lastButtonState = nowPressed;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ========== 压力传感器任务 ==========
void taskSensor(void *pvParameters) {
  while (1) {
    pressureTriggered = readPressureDebounced();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// ========== 电机任务 ==========
void taskMotor(void *pvParameters) {
  while (1) {
    updateWheelMotor(m1);
    updateWheelMotor(m2);
    updateWheelMotor(m3);

    if (ENABLE_CURRENT_PROTECTION && systemState != SYS_EMERGENCY_STOP) {
      if (checkCurrentProtection()) {
        emergencyStop = true;
        systemRunning = false;
        systemState = SYS_EMERGENCY_STOP;
        stopAllMotors();
      }
    }

    if (ENABLE_STALL_PROTECTION && systemRunning && !emergencyStop) {
      if (checkStallProtection()) {
        emergencyStop = true;
        systemRunning = false;
        systemState = SYS_EMERGENCY_STOP;
        stopAllMotors();
      }
    }

    if (systemRunning && !emergencyStop) {
      workflowStep();
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ========== 调试任务 ==========
void taskDebug(void *pvParameters) {
  while (1) {
    Serial.println();
    Serial.println("========== SYSTEM DEBUG ==========");

    Serial.print("[RUN] ");
    Serial.print(systemRunning);
    Serial.print(" | [EMG] ");
    Serial.print(emergencyStop);
    Serial.print(" | [SYS] ");
    Serial.print(systemStateName(systemState));
    Serial.print(" | [ACTION] ");
    Serial.print(actionStateName(action.state));
    Serial.print(" | [Pressure] ");
    Serial.println(pressureTriggered);

    Serial.print("[M1 total] ");
    Serial.print(m1.totalPos);
    Serial.print(" target=");
    Serial.print(m1.targetPos);
    Serial.print(" progress=");
    Serial.println(m1Progress);

    Serial.print("[M2 total] ");
    Serial.print(m2.totalPos);
    Serial.print(" target=");
    Serial.print(m2.targetPos);
    Serial.print(" progress=");
    Serial.print(m2Progress);
    Serial.print(" action=");
    Serial.print(m2Progress / M2_STEP);
    Serial.print("/");
    Serial.println(TEST_M2_ACTIONS_BEFORE_M1);

    Serial.print("[M3 total] ");
    Serial.print(m3.totalPos);
    Serial.print(" target=");
    Serial.print(m3.targetPos);
    Serial.print(" actionHome=");
    Serial.println(action.m3HomeTotal);

    Serial.print("[M4 home] ");
    Serial.print(m4.homePos);
    Serial.print(" target=");
    Serial.println(m4.targetAbsPos);

    Serial.println("---------- SERVO FEEDBACK ----------");
    printServoStatus(M1_ID);
    printServoStatus(M2_ID);
    printServoStatus(M3_ID);
    printServoStatus(M4_ID);

    Serial.println("====================================");

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// ========== 打印舵机状态 ==========
void printServoStatus(int id) {
  int pos = st.ReadPos(id);
  int speed = st.ReadSpeed(id);
  int current = st.ReadCurrent(id);
  int load = st.ReadLoad(id);
  int voltage = st.ReadVoltage(id);
  int temper = st.ReadTemper(id);
  int move = st.ReadMove(id);

  Serial.print("ID ");
  Serial.print(id);
  Serial.print(" | Pos=");
  Serial.print(pos);
  Serial.print(" | Speed=");
  Serial.print(speed);
  Serial.print(" | Current=");
  Serial.print(current);
  Serial.print(" | Load=");
  Serial.print(load);
  Serial.print(" | Voltage=");
  Serial.print(voltage);
  Serial.print(" | Temper=");
  Serial.print(temper);
  Serial.print(" | Move=");
  Serial.println(move);
}

// ========== 初始化电机 ==========
void initMotor(Motor &m, int id, MotorMode mode, float kp, float kv) {
  m.id = id;
  m.mode = mode;

  int pos = st.ReadPos(id);
  if (pos < 0) pos = 0;

  m.lastPos = pos;
  m.totalPos = 0;
  m.targetPos = 0;

  m.velocity = 0;
  m.lastTotalPos = 0;
  m.lastVelTime = millis();

  m.homePos = pos;
  m.targetAbsPos = pos;

  m.done = false;

  m.Kp = kp;
  m.Kv = kv;

  m.lastCmdSpeed = 0;

  m.motionStartTime = 0;
  m.stallStartTime = 0;
}

// ========== 多圈位置更新 ==========
void updateWheelMotor(Motor &m) {
  if (m.mode != WHEEL_MODE) return;

  int pos = st.ReadPos(m.id);
  if (pos < 0) return;

  int delta = pos - m.lastPos;

  if (delta > 2048) delta -= 4096;
  if (delta < -2048) delta += 4096;

  m.totalPos += delta;
  m.lastPos = pos;

  unsigned long now = millis();
  if (now - m.lastVelTime >= 20) {
    float dt = (now - m.lastVelTime) / 1000.0f;
    if (dt > 0) {
      m.velocity = (m.totalPos - m.lastTotalPos) / dt;
      m.lastTotalPos = m.totalPos;
      m.lastVelTime = now;
    }
  }
}

// ========== 轮子模式控制 ==========
bool controlWheelToTarget(Motor &m) {
  long error = m.targetPos - m.totalPos;

  if (abs(error) < tolerance && abs(m.velocity) < 60) {
    st.WriteSpe(m.id, 0);
    m.lastCmdSpeed = 0;

    if (ENABLE_CMD_PRINT) {
      Serial.print("CMD -> ID ");
      Serial.print(m.id);
      Serial.println(" Speed=0 DONE");
    }

    return true;
  }

  float output = m.Kp * error - m.Kv * m.velocity;

  if (abs(error) < 500) {
    output *= 0.7f;
  }

  int speed = (int)output;

  if (speed > wheelMaxSpeed) speed = wheelMaxSpeed;
  if (speed < -wheelMaxSpeed) speed = -wheelMaxSpeed;

  if (speed > 0 && speed < wheelMinSpeed) speed = wheelMinSpeed;
  if (speed < 0 && speed > -wheelMinSpeed) speed = -wheelMinSpeed;

  st.WriteSpe(m.id, speed);
  m.lastCmdSpeed = speed;

  if (ENABLE_CMD_PRINT) {
    Serial.print("CMD -> ID ");
    Serial.print(m.id);
    Serial.print(" Speed=");
    Serial.print(speed);
    Serial.print(" Error=");
    Serial.print(error);
    Serial.print(" Vel=");
    Serial.println(m.velocity);
  }

  return false;
}

// ========== 位置模式控制 ==========
bool controlPositionToTarget(Motor &m) {
  int posNow = st.ReadPos(m.id);
  if (posNow < 0) return false;

  int error = m.targetAbsPos - posNow;

  if (abs(error) < 8) {
    if (ENABLE_CMD_PRINT) {
      Serial.print("CMD -> ID ");
      Serial.print(m.id);
      Serial.println(" Position DONE");
    }
    return true;
  }

  st.WritePosEx(m.id, m.targetAbsPos, 1200, 50);

  if (ENABLE_CMD_PRINT) {
    Serial.print("CMD -> ID ");
    Serial.print(m.id);
    Serial.print(" PosTarget=");
    Serial.print(m.targetAbsPos);
    Serial.print(" Error=");
    Serial.println(error);
  }

  return false;
}

// ========== 设置目标 ==========
void setWheelTargetRelative(Motor &m, long delta) {
  m.targetPos = m.totalPos + delta;
  m.done = false;
  m.motionStartTime = millis();
  m.stallStartTime = 0;
}

void setPositionTargetRelativeFromHome(Motor &m, int delta) {
  m.targetAbsPos = wrapPos(m.homePos + delta);
  m.done = false;
}

// ========== 启动 / 暂停 / 继续 ==========
void handleButtonPress() {
  if (emergencyStop) {
    Serial.println("Emergency stop is active. Please reset the controller after checking hardware.");
    return;
  }

  if (!workflowStarted) {
    beginWorkflow();
    return;
  }

  if (systemState == SYS_PAUSED) {
    resumeSystem();
  } else if (systemRunning) {
    pauseSystem();
  }
}

void beginWorkflow() {
  emergencyStop = false;
  systemRunning = true;
  workflowStarted = true;

  m1Progress = 0;
  m2Progress = 0;

  m1.targetPos = m1.totalPos;
  m2.targetPos = m2.totalPos;
  m3.targetPos = m3.totalPos;

  action.state = ACTION_IDLE;
  action.active = false;
  action.done = false;

  startCutAction();
  systemState = SYS_RUN_ACTION;

  Serial.println("Workflow started.");
}

void pauseSystem() {
  stateBeforePause = systemState;
  pausedM4Target = m4.targetAbsPos;
  systemRunning = false;
  systemState = SYS_PAUSED;
  stopAllMotors();
  Serial.println("Workflow paused.");
}

void resumeSystem() {
  systemRunning = true;
  systemState = stateBeforePause;

  if (action.state == ACTION_M3_APPROACH) {
    st.WriteSpe(m3.id, m3ApproachSpeed);
  }

  if (isM4ActionState(action.state)) {
    m4.targetAbsPos = pausedM4Target;
  }

  Serial.println("Workflow resumed.");
}

bool isM4ActionState(ActionState s) {
  return s == ACTION_M4_LEFT || s == ACTION_M4_RIGHT || s == ACTION_M4_HOME;
}

// ========== 单次动作 ==========
void startCutAction() {
  action.active = true;
  action.done = false;
  action.m3HomeTotal = m3.totalPos;
  action.m3TouchPos = 0;

  setWheelTargetRelative(m2, M2_STEP);
  action.state = ACTION_M2_STEP;
}

void runCutAction() {
  switch (action.state) {
    case ACTION_IDLE:
      break;

    case ACTION_M2_STEP:
      m2.done = controlWheelToTarget(m2);
      if (m2.done) {
        m2Progress += M2_STEP;
        st.WriteSpe(m3.id, m3ApproachSpeed);

        if (ENABLE_CMD_PRINT) {
          Serial.print("CMD -> ID ");
          Serial.print(m3.id);
          Serial.print(" ApproachSpeed=");
          Serial.println(m3ApproachSpeed);
        }

        action.state = ACTION_M3_APPROACH;
      }
      break;

    case ACTION_M3_APPROACH:
      st.WriteSpe(m3.id, m3ApproachSpeed);

      if (pressureTriggered) {
        st.WriteSpe(m3.id, 0);
        action.m3TouchPos = m3.totalPos;
        m3.targetPos = action.m3TouchPos + M3_RETRACT_DELTA;
        m3.done = false;
        action.state = ACTION_M3_RETRACT;
        Serial.println("Pressure detected, M3 retract.");
      }
      break;

    case ACTION_M3_RETRACT:
      m3.done = controlWheelToTarget(m3);
      if (m3.done) {
        setPositionTargetRelativeFromHome(m4, M4_LEFT_DELTA);
        action.state = ACTION_M4_LEFT;
      }
      break;

    case ACTION_M4_LEFT:
      m4.done = controlPositionToTarget(m4);
      if (m4.done) {
        setPositionTargetRelativeFromHome(m4, M4_RIGHT_DELTA);
        action.state = ACTION_M4_RIGHT;
      }
      break;

    case ACTION_M4_RIGHT:
      m4.done = controlPositionToTarget(m4);
      if (m4.done) {
        setPositionTargetRelativeFromHome(m4, 0);
        action.state = ACTION_M4_HOME;
      }
      break;

    case ACTION_M4_HOME:
      m4.done = controlPositionToTarget(m4);
      if (m4.done) {
        m3.targetPos = action.m3HomeTotal;
        m3.done = false;
        action.state = ACTION_M3_HOME;
      }
      break;

    case ACTION_M3_HOME:
      m3.done = controlWheelToTarget(m3);
      if (m3.done) {
        finishCutAction();
      }
      break;

    case ACTION_DONE:
      break;
  }
}

void finishCutAction() {
  action.active = false;
  action.done = true;
  action.state = ACTION_DONE;
  Serial.println("Cut action done.");
}

// ========== 总工作流 ==========
void workflowStep() {
  switch (systemState) {
    case SYS_WAIT_START:
    case SYS_PAUSED:
    case SYS_EMERGENCY_STOP:
      break;

    case SYS_RUN_ACTION:
      runCutAction();
      if (action.done) {
        if (m2Progress < M2_TOTAL) {
          startCutAction();
        } else if (abs(m1Progress) < abs(M1_TOTAL)) {
          setWheelTargetRelative(m1, M1_STEP);
          systemState = SYS_M1_STEP;
        } else {
          systemState = SYS_FINISHED;
        }
      }
      break;

    case SYS_M1_STEP:
      m1.done = controlWheelToTarget(m1);
      if (m1.done) {
        m1Progress += M1_STEP;
        m2Progress = 0;
        if (abs(m1Progress) >= abs(M1_TOTAL)) {
          systemState = SYS_FINISHED;
        } else {
          startCutAction();
          systemState = SYS_RUN_ACTION;
        }
      }
      break;

    case SYS_FINISHED:
      stopAllMotors();
      systemRunning = false;
      workflowStarted = false;
      Serial.println("M1-M4 workflow finished.");
      break;
  }
}

// ========== 电流保护 ==========
bool checkCurrentProtection() {
  int ids[4] = {M1_ID, M2_ID, M3_ID, M4_ID};

  for (int i = 0; i < 4; i++) {
    int current = st.ReadCurrent(ids[i]);

    if (current != -1 && current > CURRENT_LIMIT) {
      Serial.print("Over current! ID=");
      Serial.print(ids[i]);
      Serial.print(" Current=");
      Serial.println(current);
      return true;
    }
  }

  return false;
}

// ========== 堵转 / 到位失败保护 ==========
bool checkStallProtection() {
  if (systemState == SYS_M1_STEP) {
    return checkWheelMotorStall(m1, "M1", M1_MOVE_TIMEOUT_MS);
  }

  if (systemState == SYS_RUN_ACTION && action.state == ACTION_M2_STEP) {
    return checkWheelMotorStall(m2, "M2", M2_MOVE_TIMEOUT_MS);
  }

  return false;
}

bool checkWheelMotorStall(Motor &m, const char *name, unsigned long timeoutMs) {
  if (m.done || m.motionStartTime == 0) return false;

  unsigned long now = millis();
  long error = m.targetPos - m.totalPos;
  long absError = abs(error);

  if (absError <= tolerance * 2) {
    m.stallStartTime = 0;
    return false;
  }

  if (now - m.motionStartTime > timeoutMs) {
    Serial.print("Move timeout! ");
    Serial.print(name);
    Serial.print(" error=");
    Serial.print(error);
    Serial.print(" total=");
    Serial.print(m.totalPos);
    Serial.print(" target=");
    Serial.println(m.targetPos);
    return true;
  }

  if (now - m.motionStartTime < STALL_GRACE_MS) {
    return false;
  }

  bool lowVelocity = abs(m.velocity) < STALL_VELOCITY_LIMIT;
  bool commandingMove = abs(m.lastCmdSpeed) >= wheelMinSpeed;

  if (lowVelocity && commandingMove) {
    if (m.stallStartTime == 0) {
      m.stallStartTime = now;
    }

    if (now - m.stallStartTime > STALL_CONFIRM_MS) {
      Serial.print("Stall detected! ");
      Serial.print(name);
      Serial.print(" error=");
      Serial.print(error);
      Serial.print(" velocity=");
      Serial.print(m.velocity);
      Serial.print(" cmd=");
      Serial.println(m.lastCmdSpeed);
      return true;
    }
  } else {
    m.stallStartTime = 0;
  }

  return false;
}

// ========== 停止所有电机 ==========
void stopWheelMotor(Motor &m) {
  st.WriteSpe(m.id, 0);
  m.lastCmdSpeed = 0;
}

void stopAllMotors() {
  stopWheelMotor(m1);
  stopWheelMotor(m2);
  stopWheelMotor(m3);

  int pos = st.ReadPos(m4.id);
  if (pos >= 0) {
    st.WritePosEx(m4.id, pos, 0, 50);
    m4.targetAbsPos = pos;
  }
}

// ========== 按钮 / 压力消抖 ==========
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

bool readPressureDebounced() {
  static bool stableState = false;
  static bool lastRawState = false;
  static unsigned long lastChangeTime = 0;

  bool rawState = (digitalRead(PRESSURE_PIN) == PRESSURE_ACTIVE);

  if (rawState != lastRawState) {
    lastRawState = rawState;
    lastChangeTime = millis();
  }

  if (millis() - lastChangeTime >= 15) {
    stableState = rawState;
  }

  return stableState;
}

// ========== 工具函数 ==========
int wrapPos(int pos) {
  while (pos < 0) pos += 4096;
  while (pos > 4095) pos -= 4096;
  return pos;
}

const char* systemStateName(SystemState s) {
  switch (s) {
    case SYS_WAIT_START: return "WAIT_START";
    case SYS_RUN_ACTION: return "RUN_ACTION";
    case SYS_M1_STEP: return "M1_STEP";
    case SYS_PAUSED: return "PAUSED";
    case SYS_FINISHED: return "FINISHED";
    case SYS_EMERGENCY_STOP: return "EMERGENCY_STOP";
    default: return "UNKNOWN";
  }
}

const char* actionStateName(ActionState s) {
  switch (s) {
    case ACTION_IDLE: return "IDLE";
    case ACTION_M2_STEP: return "M2_STEP";
    case ACTION_M3_APPROACH: return "M3_APPROACH";
    case ACTION_M3_RETRACT: return "M3_RETRACT";
    case ACTION_M4_LEFT: return "M4_LEFT";
    case ACTION_M4_RIGHT: return "M4_RIGHT";
    case ACTION_M4_HOME: return "M4_HOME";
    case ACTION_M3_HOME: return "M3_HOME";
    case ACTION_DONE: return "DONE";
    default: return "UNKNOWN";
  }
}
