#include <Arduino.h>
#include <SCServo.h>
SMS_STS st;                         // ST 系列电机对象
SCSCL sc;                           // SC 系列电机对象
#define RX 44                       // 舵机串口接收脚
#define TX 43                       // 舵机串口发送脚
#define BUTTON_PIN 46               // 启停按钮引脚
#define PRESSURE_PIN 45             // 压力传感器引脚
#define BUTTON_ACTIVE LOW           // 按钮低电平有效
#define PRESSURE_ACTIVE LOW         // 压力信号低电平有效
#define M1_ID 1                     // M1 电机 ID
#define M2_ID 2                     // M2 电机 ID
#define M3_ID 3                     // M3 电机 ID
#define M4_ID 4                     // M4 电机 ID
#define M5_ID 5                     // M5 电机 ID
#define ONE_TURN 4096L              // 一圈对应的编码值
// 主要行程参数
const long M1_STEP = -ONE_TURN / 4;                         // M1 每次推进量
const long M1_TOTAL = -ONE_TURN * 7;                        // M1 总推进量
const long M2_INITIAL_LIFT = -ONE_TURN / 8;                 // M2 回零后先抬升量
const long M2_STEP = -ONE_TURN / 20;                        // M2 每次上升量
const long M2_LAYER_TOTAL = ONE_TURN * 3;                   // M2 一层累计量
const int M3_APPROACH_SPEED = 120;                          // M3 接近速度
const long M3_APPROACH_MAX_TRAVEL = ONE_TURN * 2;           // M3 最大接近行程
const long M3_SMALL_RETRACT_DELTA = -ONE_TURN / 20;         // M3 小回退量
const int M3_RETRACT_RELEASE_SPEED = -300;                  // M3 回退起步速度
const unsigned long M3_RETRACT_RELEASE_MS = 800;            // M3 回退释放时间
const int M4_CENTER_POS = 2048;                             // M4 中心位置
const int M4_CCW_TARGET = 568;                              // M4 逆时针目标
const int M4_CW_TARGET = 3528;                              // M4 顺时针目标
const int M4_POSITION_SPEED = 500;                          // M4 位置模式速度
const int M4_POSITION_ACC = 50;                             // M4 位置模式加速度
const int M4_POSITION_TOLERANCE = 8;                        // M4 到位误差
const int M5_HOLD_POS = 820;                                // M5 保持位置
const int M5_TARGET_POS = 700;                              // M5 动作位置
const int M5_POSITION_SPEED = 400;                          // M5 位置速度
const int M5_POSITION_TIME = 0;                             // M5 位置时间参数
const int M5_POSITION_TOLERANCE = 4;                        // M5 到位误差
int wheelMaxSpeed = 700;                                    // 轮模式最大速度
int wheelMinSpeed = 120;                                    // 轮模式最小速度
int wheelTolerance = 35;                                    // 普通轮模式到位误差
const int M2_UP_MIN_SPEED = 140;                            // M2 上升最小速度
const int M2_DOWN_MIN_SPEED = 90;                           // M2 下降最小速度
const int M2_UP_GRAVITY_COMP = 50;                          // M2 上升补偿
const int M2_SPEED_SLEW_STEP = 30;                          // M2 速度变化限制
const int M2_WHEEL_TOLERANCE = 25;                          // M2 到位误差
const int M2_UP_OVERSHOOT_TOLERANCE = 80;                   // M2 上升允许过冲
const int M2_DONE_VELOCITY_LIMIT = 45;                      // M2 完成时速度上限
const long M2_MIN_EFFECTIVE_TRAVEL = (ONE_TURN / 20) - 30;  // M2 最小有效行程
const int M2_BOTTOM_RELEASE_SPEED = -320;                   // M2 离底释放速度
const unsigned long M2_BOTTOM_RELEASE_MS = 800;             // M2 离底释放时间
const int M2_HOMING_DOWN_SPEED = 150;                       // M2 回零下行速度
const int M2_HOMING_DOWN_PROBE_SPEED = 220;                 // M2 回零探测速度
const int M2_HOMING_CURRENT_LIMIT = 77;                     // M2 回零电流保护
const int M2_HOMING_BOTTOM_CURRENT = 10;                    // M2 底部电流信号
const int M2_HOMING_BOTTOM_LOAD_DELTA = 60;                 // M2 底部负载变化
const int M2_HOMING_BOTTOM_VELOCITY_LIMIT = 35;             // M2 底部速度阈值
const unsigned long M2_HOMING_BOTTOM_CONFIRM_MS = 200;      // M2 底部确认时间
const unsigned long M2_HOMING_TIMEOUT_MS = 60000;           // M2 回零超时
const unsigned long M2_HOMING_NO_MOVE_TIMEOUT_MS = 1500;    // M2 回零不动超时
const long M2_HOMING_MIN_TRAVEL = ONE_TURN / 16;            // M2 最小回零行程
const long M2_HOMING_MAX_TRAVEL = ONE_TURN * 8;             // M2 最大回零行程
const int GLOBAL_CURRENT_LIMIT = 1000;                      // 总电流保护
const unsigned long GLOBAL_CURRENT_CONFIRM_MS = 200;        // 总电流确认时间
const int ST_CURRENT_LIMIT = 2000;                          // ST 电机默认电流保护
const int M5_CURRENT_LIMIT = 350;                           // M5 电流保护
const unsigned long M5_CURRENT_CONFIRM_MS = 200;            // M5 电流确认时间
const unsigned long M1_MOVE_TIMEOUT_MS = 8000;              // M1 动作超时
const unsigned long M2_MOVE_TIMEOUT_MS = 15000;             // M2 动作超时
const unsigned long M3_MOVE_TIMEOUT_MS = 30000;             // M3 动作超时
const unsigned long M4_MOVE_TIMEOUT_MS = 8000;              // M4 动作超时
const unsigned long M5_MOVE_TIMEOUT_MS = 8000;              // M5 动作超时
const unsigned long STALL_GRACE_MS = 500;                   // 堵转检测延迟
const unsigned long M2_STALL_GRACE_MS = 3000;               // M2 堵转检测延迟
const unsigned long STALL_CONFIRM_MS = 1000;                // 堵转确认时间
const int STALL_VELOCITY_LIMIT = 25;                        // 堵转速度阈值
const unsigned long STEP_DWELL_MS = 300;                    // 动作间停顿
const unsigned long DEBUG_INTERVAL_MS = 1000;               // 调试打印间隔
bool ENABLE_CURRENT_PROTECTION = true;                      // 是否启用电流保护
bool ENABLE_STALL_PROTECTION = true;                        // 是否启用堵转保护
bool ENABLE_CMD_PRINT = true;                               // 是否打印速度命令
// 电机控制模式
enum MotorMode {
  WHEEL_MODE,
  POSITION_MODE
};
struct Motor {
  int id;                         // 电机 ID
  MotorMode mode;                 // 控制模式
  int lastPos;                    // 上一次原始位置
  long totalPos;                  // 累计位置
  long targetPos;                 // 轮模式目标位置
  float velocity;                 // 当前估算速度
  long lastTotalPos;              // 上次测速位置
  unsigned long lastVelTime;      // 上次测速时间
  int homePos;                    // 初始位置
  int targetAbsPos;               // 位置模式目标
  bool done;                      // 当前动作是否完成
  float Kp;                       // 位置比例系数
  float Kv;                       // 速度阻尼系数
  int lastCmdSpeed;               // 上一次命令速度
  unsigned long motionStartTime;  // 动作开始时间
  unsigned long stallStartTime;   // 堵转开始时间
  long motionStartTotal;          // 动作开始时累计位置
};
Motor m1, m2, m3, m4;
enum ActionState {
  ACTION_IDLE,
  ACTION_M2_STEP,
  ACTION_M3_APPROACH,
  ACTION_M3_STOP_AT_PRESSURE,
  ACTION_M4_CCW,
  ACTION_M4_CW,
  ACTION_M3_SMALL_RETRACT,
  ACTION_M5_TO_700,
  ACTION_M5_BACK_820,
  ACTION_M3_HOME,
  ACTION_DONE
};
struct CutAction {
  ActionState state;              // 当前动作状态
  bool active;                    // 动作是否进行中
  bool done;                      // 整轮动作是否完成
  long m3HomeTotal;               // 本轮 M3 起点
  long m3TouchTotal;              // M3 压力触发位置
  int m5Target;                   // M5 当前目标
  unsigned long stateStartTime;   // 状态开始时间
  unsigned long dwellUntil;       // 等待结束时间
};
CutAction action;
// 主流程状态
enum SystemState {
  SYS_WAIT_START,
  SYS_M2_HOMING_DOWN,
  SYS_M2_HOME_HOLD,
  SYS_M2_INITIAL_LIFT,
  SYS_PREPARE_M5,
  SYS_RUN_ACTION,
  SYS_M1_STEP,
  SYS_PAUSED,
  SYS_FINISHED,
  SYS_EMERGENCY_STOP
};
volatile bool pressureTriggered = false;              // 压力触发状态
bool systemRunning = false;                           // 系统是否运行
bool emergencyStop = false;                           // 是否急停
bool workflowStarted = false;                         // 流程是否已启动
bool lastButtonState = false;                         // 上一次按钮状态
SystemState systemState = SYS_WAIT_START;             // 当前系统状态
SystemState stateBeforePause = SYS_WAIT_START;        // 暂停前系统状态
ActionState actionStateBeforePause = ACTION_IDLE;     // 暂停前动作状态
int pausedM4Target = M4_CENTER_POS;                   // 暂停时 M4 目标
int pausedM5Target = M5_HOLD_POS;                     // 暂停时 M5 目标
long m1Progress = 0;                                  // M1 累计进度
long m2LayerProgress = 0;                             // M2 当前层累计进度
long m2HomingStartTotal = 0;                          // M2 回零起点
int m2HomingStartCurrent = 0;                         // M2 回零起始电流
int m2HomingStartLoad = 0;                            // M2 回零起始负载
unsigned long m2HomingNoMoveStartTime = 0;            // M2 回零不动计时
unsigned long m2HomingBottomSignalStartTime = 0;      // M2 底部信号计时
bool m2HomingProbeBoosted = false;                    // M2 是否已提高探测速度
unsigned long lastDebugTime = 0;                      // 上次调试打印时间
unsigned long globalCurrentOverStartTime = 0;         // 总电流超限计时
unsigned long m5CurrentOverStartTime = 0;             // M5 电流超限计时
TaskHandle_t taskButtonHandle = NULL;                 // 按钮任务句柄
TaskHandle_t taskSensorHandle = NULL;                 // 传感器任务句柄
TaskHandle_t taskMotorHandle = NULL;                  // 电机任务句柄
TaskHandle_t taskDebugHandle = NULL;                  // 调试任务句柄
void taskButton(void *pvParameters);
void taskSensor(void *pvParameters);
void taskMotor(void *pvParameters);
void taskDebug(void *pvParameters);
void initMotor(Motor &m, int id, MotorMode mode, float kp = 1.2f, float kv = 1.0f);
void updateWheelMotor(Motor &m);
bool controlWheelToTarget(Motor &m);
bool controlM4ToTarget();
bool controlM5ToTarget();
void setWheelTargetRelative(Motor &m, long delta);
void setM4Target(int target, const char *label);
void setM5Target(int target, const char *label);
void beginWorkflow();
void pauseSystem();
void resumeSystem();
void handleButtonPress();
void startCutAction();
void enterActionState(ActionState nextState);
void runCutAction();
void finishCutAction();
void workflowStep();
void enterM2HomingDown();
void runM2HomingDown();
void runM2HomeHold();
void enterM2InitialLift();
void runM2InitialLift();
void resetM2BottomAsZero();
bool checkM2HomingBottomDetected();
bool readButtonDebounced();
bool readPressureDebounced();
void stopAllMotors();
void stopWheelMotor(Motor &m);
void stopM4AtCurrent();
void stopM5AtCurrent();
bool checkCurrentProtection();
bool checkStallProtection();
bool checkWheelMotorStall(Motor &m, const char *name, unsigned long timeoutMs);
bool checkActionTimeout(unsigned long timeoutMs, const char *name);
void printServoStatusST(int id);
void printServoStatusM5();
int wrapPos(int pos);
int circularError(int target, int current);
long absLong(long value);
float absFloat(float value);
const char* systemStateName(SystemState s);
const char* actionStateName(ActionState s);
// setup：初始化串口、电机、IO 和任务
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial1.begin(1000000, SERIAL_8N1, RX, TX);
  st.pSerial = &Serial1;
  sc.pSerial = &Serial1;
  delay(1000);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(PRESSURE_PIN, INPUT_PULLUP);
  Serial.println("Checking servo communication...");
  for (int id = 1; id <= 4; id++) {
    int pos = st.ReadPos(id);
    Serial.print("ST ID ");
    Serial.print(id);
    Serial.print(" Pos = ");
    Serial.println(pos);
  }
  Serial.print("SC09 ID ");
  Serial.print(M5_ID);
  Serial.print(" Pos = ");
  Serial.println(sc.ReadPos(M5_ID));
  initMotor(m1, M1_ID, WHEEL_MODE, 1.2f, 1.0f);
  initMotor(m2, M2_ID, WHEEL_MODE, 1.0f, 0.45f);
  initMotor(m3, M3_ID, WHEEL_MODE, 1.25f, 0.35f);
  initMotor(m4, M4_ID, POSITION_MODE);
  m4.homePos = M4_CENTER_POS;
  m4.targetAbsPos = M4_CENTER_POS;
  action.state = ACTION_IDLE;
  action.active = false;
  action.done = false;
  action.m3HomeTotal = 0;
  action.m3TouchTotal = 0;
  action.m5Target = M5_HOLD_POS;
  action.stateStartTime = 0;
  action.dwellUntil = 0;
  Serial.println();
  Serial.println("========== M1-M5 Five Motor Workflow Ready ==========");
  Serial.println("One cycle: M2 up -> M3 approach -> M4 CCW/CW -> M3 small retract -> M5 820/700/820 -> M3 home.");
  Serial.println("M3 approach speed is slow. M4 uses calibrated limits: 568 and 3528.");
  Serial.println("Button: first press start, next press pause, next press resume.");
  Serial.println("=====================================================");
  xTaskCreatePinnedToCore(taskButton, "ButtonTask", 2048, NULL, 3, &taskButtonHandle, 0);
  xTaskCreatePinnedToCore(taskSensor, "SensorTask", 2048, NULL, 3, &taskSensorHandle, 0);
  xTaskCreatePinnedToCore(taskMotor, "MotorTask", 8192, NULL, 4, &taskMotorHandle, 1);
  xTaskCreatePinnedToCore(taskDebug, "DebugTask", 4096, NULL, 1, &taskDebugHandle, 1);
}
// loop：主循环空转，实际工作由任务执行
void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
// taskButton：循环读取按钮并处理按下事件
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
// taskSensor：循环读取压力传感器状态
void taskSensor(void *pvParameters) {
  while (1) {
    pressureTriggered = readPressureDebounced();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
// taskMotor：更新电机位置、保护逻辑和主流程
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
// taskDebug：定时输出系统状态和电机反馈
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
    Serial.print("[M1] total=");
    Serial.print(m1.totalPos);
    Serial.print(" target=");
    Serial.print(m1.targetPos);
    Serial.print(" progress=");
    Serial.println(m1Progress);
    Serial.print("[M2] total=");
    Serial.print(m2.totalPos);
    Serial.print(" target=");
    Serial.print(m2.targetPos);
    Serial.print(" stepTravel=");
    Serial.print(m2.totalPos - m2.motionStartTotal);
    Serial.print(" layerProgress=");
    Serial.print(m2LayerProgress);
    Serial.print("/");
    Serial.println(M2_LAYER_TOTAL);
    if (systemState == SYS_M2_HOMING_DOWN) {
      Serial.print("[M2 homing] travel=");
      Serial.print(absLong(m2.totalPos - m2HomingStartTotal));
      Serial.print(" probe=");
      Serial.print(m2HomingProbeBoosted);
      Serial.print(" startCurrent=");
      Serial.print(m2HomingStartCurrent);
      Serial.print(" startLoad=");
      Serial.println(m2HomingStartLoad);
    }
    Serial.print("[M3] total=");
    Serial.print(m3.totalPos);
    Serial.print(" target=");
    Serial.print(m3.targetPos);
    Serial.print(" actionHome=");
    Serial.print(action.m3HomeTotal);
    Serial.print(" touch=");
    Serial.println(action.m3TouchTotal);
    Serial.print("[M4] target=");
    Serial.println(m4.targetAbsPos);
    Serial.print("[M5] target=");
    Serial.println(action.m5Target);
    Serial.println("---------- SERVO FEEDBACK ----------");
    printServoStatusST(M1_ID);
    printServoStatusST(M2_ID);
    printServoStatusST(M3_ID);
    printServoStatusST(M4_ID);
    printServoStatusM5();
    Serial.println("====================================");
    vTaskDelay(pdMS_TO_TICKS(DEBUG_INTERVAL_MS));
  }
}
// initMotor：初始化单个电机的状态参数
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
  m.motionStartTotal = 0;
}
// updateWheelMotor：读取轮模式电机位置并计算累计位置和速度
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
// controlWheelToTarget：用轮模式速度控制电机到目标位置
bool controlWheelToTarget(Motor &m) {
  long error = m.targetPos - m.totalPos;                                      // 当前位置到目标的误差
  int doneTolerance = (m.id == M2_ID) ? M2_WHEEL_TOLERANCE : wheelTolerance;  // 到位允许误差
  int doneVelocityLimit = (m.id == M2_ID) ? M2_DONE_VELOCITY_LIMIT : 60;      // 到位速度限制
  bool enoughTravel = true;                                                   // 是否已走过有效行程
  if (m.id == M2_ID) {
    enoughTravel = absLong(m.totalPos - m.motionStartTotal) >= M2_MIN_EFFECTIVE_TRAVEL;
  }
  bool m2UpMove = (m.id == M2_ID && m.targetPos < m.motionStartTotal);        // M2 是否在上升
  bool m2SmallUpOvershoot = m2UpMove && m.totalPos <= m.targetPos &&
                            absLong(error) <= M2_UP_OVERSHOOT_TOLERANCE;     // M2 小幅过冲
  if (enoughTravel &&
      (absLong(error) < doneTolerance || m2SmallUpOvershoot) &&
      absFloat(m.velocity) < doneVelocityLimit) {
    st.WriteSpe(m.id, 0);
    m.lastCmdSpeed = 0;
    if (m.id == M2_ID && ENABLE_CMD_PRINT) {
      Serial.print("CMD -> ST ID ");
      Serial.print(m.id);
      Serial.print(" Speed=0 DONE");
      Serial.print(" Error=");
      Serial.print(error);
      Serial.print(" Travel=");
      Serial.print(m.totalPos - m.motionStartTotal);
      Serial.print(" Vel=");
      Serial.println(m.velocity);
    }
    return true;
  }
  float output = m.Kp * error - m.Kv * m.velocity;                            // 速度输出
  if (absLong(error) < 500) {
    output *= 0.7f;
  }
  int speed = (int)output;                                                     // 最终命令速度
  if (speed > wheelMaxSpeed) speed = wheelMaxSpeed;
  if (speed < -wheelMaxSpeed) speed = -wheelMaxSpeed;
  if (m.id == M2_ID) {
    int directionMinSpeed = speed < 0 ? M2_UP_MIN_SPEED : M2_DOWN_MIN_SPEED;  // M2 分方向最小速度
    if (speed > 0 && speed < directionMinSpeed) speed = directionMinSpeed;
    if (speed < 0 && speed > -directionMinSpeed) speed = -directionMinSpeed;
    if (speed < 0) {
      speed -= M2_UP_GRAVITY_COMP;
      if (speed < -wheelMaxSpeed) speed = -wheelMaxSpeed;
    }
    int deltaSpeed = speed - m.lastCmdSpeed;                                  // 本次速度变化量
    if (deltaSpeed > M2_SPEED_SLEW_STEP) {
      speed = m.lastCmdSpeed + M2_SPEED_SLEW_STEP;
    } else if (deltaSpeed < -M2_SPEED_SLEW_STEP) {
      speed = m.lastCmdSpeed - M2_SPEED_SLEW_STEP;
    }
  } else {
    if (speed > 0 && speed < wheelMinSpeed) speed = wheelMinSpeed;
    if (speed < 0 && speed > -wheelMinSpeed) speed = -wheelMinSpeed;
  }
  st.WriteSpe(m.id, speed);
  m.lastCmdSpeed = speed;
  if (ENABLE_CMD_PRINT) {
    Serial.print("CMD -> ST ID ");
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
// controlM4ToTarget：控制 M4 到指定绝对位置
bool controlM4ToTarget() {
  int posNow = st.ReadPos(M4_ID);
  if (posNow < 0) return false;
  int error = circularError(m4.targetAbsPos, posNow);
  if (abs(error) <= M4_POSITION_TOLERANCE) {
    return true;
  }
  st.WritePosEx(M4_ID, m4.targetAbsPos, M4_POSITION_SPEED, M4_POSITION_ACC);
  return false;
}
// controlM5ToTarget：控制 M5 到指定绝对位置
bool controlM5ToTarget() {
  int posNow = sc.ReadPos(M5_ID);
  if (posNow < 0) {
    sc.WritePos(M5_ID, action.m5Target, M5_POSITION_TIME, M5_POSITION_SPEED);
    return false;
  }
  int error = action.m5Target - posNow;
  if (abs(error) <= M5_POSITION_TOLERANCE) {
    return true;
  }
  sc.WritePos(M5_ID, action.m5Target, M5_POSITION_TIME, M5_POSITION_SPEED);
  return false;
}
// setWheelTargetRelative：设置轮模式电机的相对目标
void setWheelTargetRelative(Motor &m, long delta) {
  m.targetPos = m.totalPos + delta;
  m.done = false;
  m.motionStartTime = millis();
  m.stallStartTime = 0;
  m.motionStartTotal = m.totalPos;
}
// setM4Target：设置 M4 目标位置并发送命令
void setM4Target(int target, const char *label) {
  m4.targetAbsPos = wrapPos(target);
  m4.done = false;
  m4.motionStartTime = millis();
  st.WritePosEx(M4_ID, m4.targetAbsPos, M4_POSITION_SPEED, M4_POSITION_ACC);
  Serial.print("M4 target ");
  Serial.print(label);
  Serial.print(" = ");
  Serial.println(m4.targetAbsPos);
}
// setM5Target：设置 M5 目标位置并发送命令
void setM5Target(int target, const char *label) {
  action.m5Target = target;
  action.stateStartTime = millis();
  sc.WritePos(M5_ID, action.m5Target, M5_POSITION_TIME, M5_POSITION_SPEED);
  Serial.print("M5 target ");
  Serial.print(label);
  Serial.print(" = ");
  Serial.println(action.m5Target);
}
// handleButtonPress：处理启动、暂停和继续
void handleButtonPress() {
  if (emergencyStop) {
    Serial.println("Emergency stop is active. Reset ESP32 after checking hardware.");
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
// beginWorkflow：开始完整工作流程
void beginWorkflow() {
  emergencyStop = false;
  systemRunning = true;
  workflowStarted = true;
  m1Progress = 0;
  m2LayerProgress = 0;
  m1.targetPos = m1.totalPos;
  m2.targetPos = m2.totalPos;
  m3.targetPos = m3.totalPos;
  action.state = ACTION_IDLE;
  action.active = false;
  action.done = false;
  action.dwellUntil = 0;
  enterM2HomingDown();
  Serial.println("Workflow started. M2 homing down first.");
}
// pauseSystem：暂停流程并停止所有电机
void pauseSystem() {
  stateBeforePause = systemState;
  actionStateBeforePause = action.state;
  pausedM4Target = m4.targetAbsPos;
  pausedM5Target = action.m5Target;
  systemRunning = false;
  systemState = SYS_PAUSED;
  stopAllMotors();
  Serial.println("Workflow paused.");
}
// resumeSystem：从暂停状态恢复流程
void resumeSystem() {
  systemRunning = true;
  systemState = stateBeforePause;
  action.state = actionStateBeforePause;
  m4.targetAbsPos = pausedM4Target;
  action.m5Target = pausedM5Target;
  action.stateStartTime = millis();
  if (action.state == ACTION_M3_APPROACH) {
    st.WriteSpe(M3_ID, M3_APPROACH_SPEED);
  }
  if (action.state == ACTION_M4_CCW || action.state == ACTION_M4_CW) {
    st.WritePosEx(M4_ID, m4.targetAbsPos, M4_POSITION_SPEED, M4_POSITION_ACC);
  }
  if (action.state == ACTION_M5_TO_700 || action.state == ACTION_M5_BACK_820 || systemState == SYS_PREPARE_M5) {
    sc.WritePos(M5_ID, action.m5Target, M5_POSITION_TIME, M5_POSITION_SPEED);
  }
  if (systemState == SYS_M2_HOMING_DOWN) {
    st.WriteSpe(M2_ID, m2HomingProbeBoosted ? M2_HOMING_DOWN_PROBE_SPEED : M2_HOMING_DOWN_SPEED);
  }
  Serial.println("Workflow resumed.");
}
// startCutAction：开始一轮切割动作
void startCutAction() {
  action.active = true;
  action.done = false;
  action.m3HomeTotal = m3.totalPos;
  action.m3TouchTotal = m3.totalPos;
  action.dwellUntil = 0;
  enterActionState(ACTION_M2_STEP);
}
// 设置每一步动作的起始目标
// enterActionState：切换到指定动作状态
void enterActionState(ActionState nextState) {
  action.state = nextState;
  action.stateStartTime = millis();
  action.dwellUntil = 0;
  switch (action.state) {
    case ACTION_M2_STEP:
      setWheelTargetRelative(m2, M2_STEP);
      Serial.println("ACTION 1: M2 step up.");
      break;
    case ACTION_M3_APPROACH:
      action.m3HomeTotal = m3.totalPos;
      st.WriteSpe(M3_ID, M3_APPROACH_SPEED);
      m3.lastCmdSpeed = M3_APPROACH_SPEED;
      Serial.println("ACTION 2: M3 slow approach until pressure trigger.");
      break;
    case ACTION_M3_STOP_AT_PRESSURE:
      st.WriteSpe(M3_ID, 0);
      m3.lastCmdSpeed = 0;
      action.m3TouchTotal = m3.totalPos;
      action.dwellUntil = millis() + STEP_DWELL_MS;
      Serial.println("ACTION 3: M3 stopped at pressure point.");
      break;
    case ACTION_M4_CCW:
      setM4Target(M4_CCW_TARGET, "ACTION4_CCW_568");
      Serial.println("ACTION 4A: M4 rotate counterclockwise to 568.");
      break;
    case ACTION_M4_CW:
      setM4Target(M4_CW_TARGET, "ACTION4B_CW_3528");
      Serial.println("ACTION 4B: M4 rotate clockwise to 3528, no center return.");
      break;
    case ACTION_M3_SMALL_RETRACT:
      m3.targetPos = action.m3TouchTotal + M3_SMALL_RETRACT_DELTA;
      m3.done = false;
      m3.motionStartTime = millis();
      m3.stallStartTime = 0;
      Serial.println("ACTION 5: M3 small retract.");
      break;
    case ACTION_M5_TO_700:
      setM5Target(M5_TARGET_POS, "ACTION6_TO_700");
      Serial.println("ACTION 6A: M5 move from 820 to 700.");
      break;
    case ACTION_M5_BACK_820:
      setM5Target(M5_HOLD_POS, "ACTION6B_BACK_820");
      Serial.println("ACTION 6B: M5 move back to 820.");
      break;
    case ACTION_M3_HOME:
      m3.targetPos = action.m3HomeTotal;
      m3.done = false;
      m3.motionStartTime = millis();
      m3.stallStartTime = 0;
      Serial.println("ACTION 7: M3 return to this cycle start position.");
      break;
    case ACTION_DONE:
      finishCutAction();
      break;
    default:
      break;
  }
}
// 执行一次完整动作循环
// runCutAction：运行当前动作状态
void runCutAction() {
  switch (action.state) {
    case ACTION_IDLE:
      break;
    case ACTION_M2_STEP:
      if (absLong(m2.totalPos - m2.motionStartTotal) < 10 &&
          millis() - action.stateStartTime < M2_BOTTOM_RELEASE_MS) {
        st.WriteSpe(M2_ID, M2_BOTTOM_RELEASE_SPEED);
        m2.lastCmdSpeed = M2_BOTTOM_RELEASE_SPEED;
        if (ENABLE_CMD_PRINT) {
          Serial.print("CMD -> ST ID 2 ReleaseSpeed=");
          Serial.print(M2_BOTTOM_RELEASE_SPEED);
          Serial.print(" Error=");
          Serial.print(m2.targetPos - m2.totalPos);
          Serial.print(" Vel=");
          Serial.println(m2.velocity);
        }
        return;
      }
      m2.done = controlWheelToTarget(m2);
      if (m2.done) {
        m2LayerProgress += absLong(M2_STEP);
        enterActionState(ACTION_M3_APPROACH);
      }
      break;
    case ACTION_M3_APPROACH:
      if (checkActionTimeout(M3_MOVE_TIMEOUT_MS, "M3 approach timeout before pressure trigger")) return;
      if (absLong(m3.totalPos - action.m3HomeTotal) > M3_APPROACH_MAX_TRAVEL) {
        Serial.print("M3 approach max travel before pressure trigger. travel=");
        Serial.println(absLong(m3.totalPos - action.m3HomeTotal));
        emergencyStop = true;
        systemRunning = false;
        systemState = SYS_EMERGENCY_STOP;
        stopAllMotors();
        return;
      }
      st.WriteSpe(M3_ID, M3_APPROACH_SPEED);
      m3.lastCmdSpeed = M3_APPROACH_SPEED;
      if (pressureTriggered) {
        enterActionState(ACTION_M3_STOP_AT_PRESSURE);
      }
      break;
    case ACTION_M3_STOP_AT_PRESSURE:
      if (millis() >= action.dwellUntil) {
        enterActionState(ACTION_M4_CCW);
      }
      break;
    case ACTION_M4_CCW:
      if (checkActionTimeout(M4_MOVE_TIMEOUT_MS, "M4 CCW timeout")) return;
      m4.done = controlM4ToTarget();
      if (m4.done) {
        enterActionState(ACTION_M4_CW);
      }
      break;
    case ACTION_M4_CW:
      if (checkActionTimeout(M4_MOVE_TIMEOUT_MS, "M4 CW timeout")) return;
      m4.done = controlM4ToTarget();
      if (m4.done) {
        enterActionState(ACTION_M3_SMALL_RETRACT);
      }
      break;
    case ACTION_M3_SMALL_RETRACT:
      if (absLong(m3.totalPos - action.m3TouchTotal) < 10 &&
          millis() - action.stateStartTime < M3_RETRACT_RELEASE_MS) {
        st.WriteSpe(M3_ID, M3_RETRACT_RELEASE_SPEED);
        m3.lastCmdSpeed = M3_RETRACT_RELEASE_SPEED;
        if (ENABLE_CMD_PRINT) {
          Serial.print("CMD -> ST ID 3 RetractReleaseSpeed=");
          Serial.print(M3_RETRACT_RELEASE_SPEED);
          Serial.print(" Error=");
          Serial.print(m3.targetPos - m3.totalPos);
          Serial.print(" Vel=");
          Serial.println(m3.velocity);
        }
        return;
      }
      m3.done = controlWheelToTarget(m3);
      if (m3.done) {
        enterActionState(ACTION_M5_TO_700);
      }
      break;
    case ACTION_M5_TO_700:
      if (checkActionTimeout(M5_MOVE_TIMEOUT_MS, "M5 to 700 timeout")) return;
      if (controlM5ToTarget()) {
        enterActionState(ACTION_M5_BACK_820);
      }
      break;
    case ACTION_M5_BACK_820:
      if (checkActionTimeout(M5_MOVE_TIMEOUT_MS, "M5 back 820 timeout")) return;
      if (controlM5ToTarget()) {
        enterActionState(ACTION_M3_HOME);
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
// finishCutAction：标记一轮切割动作完成
void finishCutAction() {
  action.active = false;
  action.done = true;
  action.state = ACTION_DONE;
  Serial.println("ACTION 8: Cut action done. Check M2 layer progress.");
}
// 总流程调度
// workflowStep：根据系统状态推进整体流程
void workflowStep() {
  switch (systemState) {
    case SYS_WAIT_START:
    case SYS_PAUSED:
    case SYS_EMERGENCY_STOP:
      break;
    case SYS_M2_HOMING_DOWN:
      runM2HomingDown();
      break;
    case SYS_M2_HOME_HOLD:
      runM2HomeHold();
      break;
    case SYS_M2_INITIAL_LIFT:
      runM2InitialLift();
      break;
    case SYS_PREPARE_M5:
      if (controlM5ToTarget()) {
        startCutAction();
        systemState = SYS_RUN_ACTION;
      } else if (millis() - action.stateStartTime > M5_MOVE_TIMEOUT_MS) {
        Serial.println("M5 prepare feedback timeout, continue because hold target was commanded.");
        startCutAction();
        systemState = SYS_RUN_ACTION;
      }
      break;
    case SYS_RUN_ACTION:
      runCutAction();
      if (action.done) {
        if (m2LayerProgress < M2_LAYER_TOTAL) {
          startCutAction();
        } else if (absLong(m1Progress) < absLong(M1_TOTAL)) {
          setWheelTargetRelative(m1, M1_STEP);
          systemState = SYS_M1_STEP;
          Serial.println("M2 layer complete. ACTION: M1 step.");
        } else {
          systemState = SYS_FINISHED;
        }
      }
      break;
    case SYS_M1_STEP:
      m1.done = controlWheelToTarget(m1);
      if (m1.done) {
        m1Progress += M1_STEP;
        m2LayerProgress = 0;
        if (absLong(m1Progress) >= absLong(M1_TOTAL)) {
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
      Serial.println("M1-M5 workflow finished.");
      break;
  }
}
// M2 回零开始
// enterM2HomingDown：进入 M2 回零状态
void enterM2HomingDown() {
  stopWheelMotor(m2);
  systemState = SYS_M2_HOMING_DOWN;
  m2HomingStartTotal = m2.totalPos;
  m2HomingStartCurrent = st.ReadCurrent(M2_ID);
  m2HomingStartLoad = st.ReadLoad(M2_ID);
  if (m2HomingStartCurrent < 0) m2HomingStartCurrent = 0;
  if (m2HomingStartLoad < 0) m2HomingStartLoad = 0;
  m2HomingNoMoveStartTime = 0;
  m2HomingBottomSignalStartTime = 0;
  m2HomingProbeBoosted = false;
  m2.motionStartTime = millis();
  m2.stallStartTime = 0;
  Serial.println("STATE: M2_HOMING_DOWN. Move down slowly, then set bottom as logical zero.");
}
// M2 向下找底部
// runM2HomingDown：执行 M2 回零动作
void runM2HomingDown() {
  unsigned long now = millis();
  if (now - m2.motionStartTime > M2_HOMING_TIMEOUT_MS) {
    Serial.println("M2 homing timeout.");
    emergencyStop = true;
    systemRunning = false;
    systemState = SYS_EMERGENCY_STOP;
    stopAllMotors();
    return;
  }
  if (absLong(m2.totalPos - m2HomingStartTotal) > M2_HOMING_MAX_TRAVEL) {
    Serial.println("M2 homing max travel reached.");
    emergencyStop = true;
    systemRunning = false;
    systemState = SYS_EMERGENCY_STOP;
    stopAllMotors();
    return;
  }
  int homingSpeed = m2HomingProbeBoosted ? M2_HOMING_DOWN_PROBE_SPEED : M2_HOMING_DOWN_SPEED;
  st.WriteSpe(M2_ID, homingSpeed);
  m2.lastCmdSpeed = homingSpeed;
  if (checkM2HomingBottomDetected()) {
    stopWheelMotor(m2);
    resetM2BottomAsZero();
    action.dwellUntil = millis() + STEP_DWELL_MS;
    systemState = SYS_M2_HOME_HOLD;
    Serial.println("STATE: M2_HOME_HOLD.");
  }
}
// runM2HomeHold：M2 回零后短暂停留
void runM2HomeHold() {
  if (millis() < action.dwellUntil) return;
  enterM2InitialLift();
}
// enterM2InitialLift：进入 M2 初始抬升状态
void enterM2InitialLift() {
  setWheelTargetRelative(m2, M2_INITIAL_LIFT);
  systemState = SYS_M2_INITIAL_LIFT;
  Serial.println("STATE: M2_INITIAL_LIFT. Move up 1/8 turn after homing.");
}
// 回零后先离开底部
// runM2InitialLift：执行 M2 初始抬升
void runM2InitialLift() {
  m2.done = controlWheelToTarget(m2);
  if (!m2.done) return;
  setM5Target(M5_HOLD_POS, "PREPARE_HOLD_820");
  systemState = SYS_PREPARE_M5;
  Serial.println("M2 initial lift done. Preparing M5 to hold position 820.");
}
// resetM2BottomAsZero：把当前位置设为 M2 逻辑零点
void resetM2BottomAsZero() {
  int pos = st.ReadPos(M2_ID);
  if (pos >= 0) {
    m2.lastPos = pos;
  }
  m2.totalPos = 0;
  m2.targetPos = 0;
  m2.velocity = 0;
  m2.lastTotalPos = 0;
  m2.lastVelTime = millis();
  m2.lastCmdSpeed = 0;
  m2.motionStartTotal = 0;
  Serial.println("M2 bottom detected. Set lowest point as logical zero.");
}
// 判断 M2 是否已经到底
// checkM2HomingBottomDetected：判断 M2 回零是否触底
bool checkM2HomingBottomDetected() {
  unsigned long now = millis();
  if (now - m2.motionStartTime < STALL_GRACE_MS) {
    return false;
  }
  long homingTravel = absLong(m2.totalPos - m2HomingStartTotal);              // 回零已走行程
  int current = st.ReadCurrent(M2_ID);                                        // 当前电流反馈
  int load = st.ReadLoad(M2_ID);                                              // 当前负载反馈
  int currentRise = current < 0 ? 0 : abs(current) - abs(m2HomingStartCurrent); // 电流上升量
  int loadRise = load < 0 ? 0 : abs(load) - abs(m2HomingStartLoad);           // 负载上升量
  bool bottomSignal = false;                                                  // 是否有底部信号
  bool stoppedAtBottom = absFloat(m2.velocity) < M2_HOMING_BOTTOM_VELOCITY_LIMIT; // 是否接近停止
  if (current != -1 && abs(current) >= M2_HOMING_BOTTOM_CURRENT) {
    bottomSignal = true;
  }
  if (load != -1 && loadRise >= M2_HOMING_BOTTOM_LOAD_DELTA) {
    bottomSignal = true;
  }
  if (homingTravel < M2_HOMING_MIN_TRAVEL) {
    if (bottomSignal && stoppedAtBottom) {
      if (m2HomingBottomSignalStartTime == 0) {
        m2HomingBottomSignalStartTime = now;
      }
      if (now - m2HomingBottomSignalStartTime >= M2_HOMING_BOTTOM_CONFIRM_MS) {
        Serial.print("M2 homing bottom near start. travel=");
        Serial.print(homingTravel);
        Serial.print(" velocity=");
        Serial.print(m2.velocity);
        Serial.print(" current=");
        Serial.print(current);
        Serial.print(" load=");
        Serial.println(load);
        return true;
      }
    } else {
      m2HomingBottomSignalStartTime = 0;
    }
    if (absFloat(m2.velocity) < STALL_VELOCITY_LIMIT) {
      if (m2HomingNoMoveStartTime == 0) {
        m2HomingNoMoveStartTime = now;
      }
      if (now - m2HomingNoMoveStartTime > M2_HOMING_NO_MOVE_TIMEOUT_MS) {
        if (!m2HomingProbeBoosted) {
          m2HomingProbeBoosted = true;
          m2HomingNoMoveStartTime = now;
          Serial.println("M2 homing no movement, probing with higher down speed.");
        } else if (bottomSignal) {
          Serial.print("M2 no movement after probe with bottom signal. travel=");
          Serial.print(homingTravel);
          Serial.print(" current=");
          Serial.print(current);
          Serial.print(" load=");
          Serial.println(load);
          return true;
        } else {
          Serial.println("M2 homing no movement before bottom signal.");
          emergencyStop = true;
          systemRunning = false;
          systemState = SYS_EMERGENCY_STOP;
          stopAllMotors();
          return false;
        }
      }
    } else {
      m2HomingNoMoveStartTime = 0;
    }
    return false;
  }
  m2HomingNoMoveStartTime = 0;
  if (bottomSignal && stoppedAtBottom) {
    if (m2HomingBottomSignalStartTime == 0) {
      m2HomingBottomSignalStartTime = now;
    }
    if (now - m2HomingBottomSignalStartTime >= M2_HOMING_BOTTOM_CONFIRM_MS) {
      Serial.print("M2 homing bottom signal. travel=");
      Serial.print(homingTravel);
      Serial.print(" velocity=");
      Serial.print(m2.velocity);
      Serial.print(" current=");
      Serial.print(current);
      Serial.print(" load=");
      Serial.println(load);
      return true;
    }
  } else {
    m2HomingBottomSignalStartTime = 0;
  }
  if (absFloat(m2.velocity) < STALL_VELOCITY_LIMIT) {
    if (m2.stallStartTime == 0) m2.stallStartTime = now;
    if (now - m2.stallStartTime > STALL_CONFIRM_MS) {
      if (bottomSignal) {
        Serial.print("M2 homing bottom by stall with bottom signal. travel=");
        Serial.print(homingTravel);
        Serial.print(" velocity=");
        Serial.print(m2.velocity);
        Serial.print(" current=");
        Serial.print(current);
        Serial.print(" load=");
        Serial.println(load);
        return true;
      }
      if (!m2HomingProbeBoosted) {
        m2HomingProbeBoosted = true;
        m2.stallStartTime = now;
        Serial.println("M2 homing stalled before bottom signal, probing with higher down speed.");
      } else {
        Serial.print("M2 homing stalled without bottom signal. travel=");
        Serial.print(homingTravel);
        Serial.print(" velocity=");
        Serial.print(m2.velocity);
        Serial.print(" current=");
        Serial.print(current);
        Serial.print(" load=");
        Serial.println(load);
        emergencyStop = true;
        systemRunning = false;
        systemState = SYS_EMERGENCY_STOP;
        stopAllMotors();
      }
    }
  } else {
    m2.stallStartTime = 0;
  }
  return false;
}
// 电流保护
// checkCurrentProtection：检查 ST 电机和 M5 是否过流
bool checkCurrentProtection() {
  int stIds[4] = {M1_ID, M2_ID, M3_ID, M4_ID};
  int globalCurrent = 0;
  bool hasAnyCurrentFeedback = false;
  for (int i = 0; i < 4; i++) {
    int current = st.ReadCurrent(stIds[i]);
    if (current != -1) {
      globalCurrent += abs(current);
      hasAnyCurrentFeedback = true;
    }
    int stCurrentLimit = ST_CURRENT_LIMIT;
    if (stIds[i] == M2_ID &&
        (systemState == SYS_M2_HOMING_DOWN ||
         systemState == SYS_M2_INITIAL_LIFT ||
         (systemState == SYS_RUN_ACTION && action.state == ACTION_M2_STEP))) {
      stCurrentLimit = M2_HOMING_CURRENT_LIMIT;
    }
    if (current != -1 && abs(current) > stCurrentLimit) {
      Serial.print("Over current! ST ID=");
      Serial.print(stIds[i]);
      Serial.print(" current=");
      Serial.print(current);
      Serial.print(" limit=");
      Serial.println(stCurrentLimit);
      return true;
    }
  }
  int m5Current = sc.ReadCurrent(M5_ID);
  if (m5Current != -1) {
    globalCurrent += abs(m5Current);
    hasAnyCurrentFeedback = true;
  }
  if (hasAnyCurrentFeedback && globalCurrent > GLOBAL_CURRENT_LIMIT) {
    unsigned long now = millis();
    if (globalCurrentOverStartTime == 0) {
      globalCurrentOverStartTime = now;
    }
    if (now - globalCurrentOverStartTime >= GLOBAL_CURRENT_CONFIRM_MS) {
      Serial.print("GLOBAL CURRENT LIMIT! total=");
      Serial.print(globalCurrent);
      Serial.print(" limit=");
      Serial.println(GLOBAL_CURRENT_LIMIT);
      globalCurrentOverStartTime = 0;
      return true;
    }
  } else {
    globalCurrentOverStartTime = 0;
  }
  if (m5Current < 0 || abs(m5Current) <= M5_CURRENT_LIMIT) {
    m5CurrentOverStartTime = 0;
    return false;
  }
  unsigned long now = millis();
  if (m5CurrentOverStartTime == 0) {
    m5CurrentOverStartTime = now;
  }
  if (now - m5CurrentOverStartTime >= M5_CURRENT_CONFIRM_MS) {
    Serial.print("Over current! M5 current=");
    Serial.println(m5Current);
    m5CurrentOverStartTime = 0;
    return true;
  }
  return false;
}
// 堵转保护
// checkStallProtection：按当前流程状态检查对应电机堵转
bool checkStallProtection() {
  if (systemState == SYS_M1_STEP) {
    return checkWheelMotorStall(m1, "M1", M1_MOVE_TIMEOUT_MS);
  }
  if (systemState == SYS_M2_INITIAL_LIFT) {
    return checkWheelMotorStall(m2, "M2 initial lift", M2_MOVE_TIMEOUT_MS);
  }
  if (systemState == SYS_RUN_ACTION && action.state == ACTION_M2_STEP) {
    return checkWheelMotorStall(m2, "M2", M2_MOVE_TIMEOUT_MS);
  }
  if (systemState == SYS_RUN_ACTION && action.state == ACTION_M3_SMALL_RETRACT) {
    return checkWheelMotorStall(m3, "M3 small retract", M3_MOVE_TIMEOUT_MS);
  }
  if (systemState == SYS_RUN_ACTION && action.state == ACTION_M3_HOME) {
    return checkWheelMotorStall(m3, "M3 home", M3_MOVE_TIMEOUT_MS);
  }
  return false;
}
// 单个轮模式电机堵转判断
// checkWheelMotorStall：判断轮模式电机是否堵转或超时
bool checkWheelMotorStall(Motor &m, const char *name, unsigned long timeoutMs) {
  if (m.done || m.motionStartTime == 0) return false;
  unsigned long now = millis();                                               // 当前时间
  long error = m.targetPos - m.totalPos;                                      // 剩余误差
  long absError = absLong(error);                                             // 误差绝对值
  int stallTolerance = (m.id == M2_ID) ? M2_WHEEL_TOLERANCE : wheelTolerance; // 堵转忽略范围
  if (absError <= stallTolerance * 2) {
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
  unsigned long graceMs = (m.id == M2_ID) ? M2_STALL_GRACE_MS : STALL_GRACE_MS; // 起步宽限时间
  if (now - m.motionStartTime < graceMs) {
    return false;
  }
  bool lowVelocity = absFloat(m.velocity) < STALL_VELOCITY_LIMIT;             // 速度是否过低
  int commandThreshold = (m.id == M2_ID) ? M2_DOWN_MIN_SPEED : wheelMinSpeed; // 有效命令阈值
  bool commandingMove = abs(m.lastCmdSpeed) >= commandThreshold;              // 是否正在命令运动
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
// checkActionTimeout：检查当前动作状态是否超时
bool checkActionTimeout(unsigned long timeoutMs, const char *name) {
  if (millis() - action.stateStartTime <= timeoutMs) {
    return false;
  }
  Serial.println(name);
  emergencyStop = true;
  systemRunning = false;
  systemState = SYS_EMERGENCY_STOP;
  stopAllMotors();
  return true;
}
// stopWheelMotor：停止一个轮模式电机
void stopWheelMotor(Motor &m) {
  st.WriteSpe(m.id, 0);
  m.lastCmdSpeed = 0;
}
// stopM4AtCurrent：让 M4 停在当前位置
void stopM4AtCurrent() {
  int pos = st.ReadPos(M4_ID);
  if (pos >= 0) {
    st.WritePosEx(M4_ID, pos, 0, M4_POSITION_ACC);
    m4.targetAbsPos = pos;
  }
}
// stopM5AtCurrent：让 M5 停在当前位置
void stopM5AtCurrent() {
  int pos = sc.ReadPos(M5_ID);
  if (pos >= 0) {
    action.m5Target = pos;
    sc.WritePos(M5_ID, pos, M5_POSITION_TIME, M5_POSITION_SPEED);
  }
}
// stopAllMotors：停止全部电机
void stopAllMotors() {
  stopWheelMotor(m1);
  stopWheelMotor(m2);
  stopWheelMotor(m3);
  stopM4AtCurrent();
  stopM5AtCurrent();
}
// readButtonDebounced：读取去抖后的按钮状态
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
// readPressureDebounced：读取去抖后的压力状态
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
// printServoStatusST：打印 ST 电机反馈
void printServoStatusST(int id) {
  int pos = st.ReadPos(id);
  int speed = st.ReadSpeed(id);
  int current = st.ReadCurrent(id);
  int load = st.ReadLoad(id);
  int voltage = st.ReadVoltage(id);
  int temper = st.ReadTemper(id);
  int move = st.ReadMove(id);
  Serial.print("ST ID ");
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
// printServoStatusM5：打印 M5 电机反馈
void printServoStatusM5() {
  int pos = -1;
  int speed = -1;
  int current = -1;
  int load = -1;
  int voltage = -1;
  int temper = -1;
  int move = -1;
  if (sc.FeedBack(M5_ID) != -1) {
    pos = sc.ReadPos(-1);
    speed = sc.ReadSpeed(-1);
    current = sc.ReadCurrent(-1);
    load = sc.ReadLoad(-1);
    voltage = sc.ReadVoltage(-1);
    temper = sc.ReadTemper(-1);
    move = sc.ReadMove(-1);
  }
  Serial.print("SC09 ID ");
  Serial.print(M5_ID);
  Serial.print(" | Pos=");
  Serial.print(pos);
  Serial.print(" | Target=");
  Serial.print(action.m5Target);
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
// wrapPos：把位置限制到 0-4095
int wrapPos(int pos) {
  while (pos < 0) pos += 4096;
  while (pos > 4095) pos -= 4096;
  return pos;
}
// circularError：计算 0-4095 环形位置误差
int circularError(int target, int current) {
  int error = target - current;
  if (error > 2048) error -= 4096;
  if (error < -2048) error += 4096;
  return error;
}
// absLong：long 类型取绝对值
long absLong(long value) {
  return value < 0 ? -value : value;
}
// absFloat：float 类型取绝对值
float absFloat(float value) {
  return value < 0 ? -value : value;
}
// systemStateName：把系统状态转成文字
const char* systemStateName(SystemState s) {
  switch (s) {
    case SYS_WAIT_START: return "WAIT_START";
    case SYS_M2_HOMING_DOWN: return "M2_HOMING_DOWN";
    case SYS_M2_HOME_HOLD: return "M2_HOME_HOLD";
    case SYS_M2_INITIAL_LIFT: return "M2_INITIAL_LIFT";
    case SYS_PREPARE_M5: return "PREPARE_M5";
    case SYS_RUN_ACTION: return "RUN_ACTION";
    case SYS_M1_STEP: return "M1_STEP";
    case SYS_PAUSED: return "PAUSED";
    case SYS_FINISHED: return "FINISHED";
    case SYS_EMERGENCY_STOP: return "EMERGENCY_STOP";
    default: return "UNKNOWN";
  }
}
// actionStateName：把动作状态转成文字
const char* actionStateName(ActionState s) {
  switch (s) {
    case ACTION_IDLE: return "IDLE";
    case ACTION_M2_STEP: return "M2_STEP";
    case ACTION_M3_APPROACH: return "M3_APPROACH";
    case ACTION_M3_STOP_AT_PRESSURE: return "M3_STOP_AT_PRESSURE";
    case ACTION_M4_CCW: return "M4_CCW";
    case ACTION_M4_CW: return "M4_CW";
    case ACTION_M3_SMALL_RETRACT: return "M3_SMALL_RETRACT";
    case ACTION_M5_TO_700: return "M5_TO_700";
    case ACTION_M5_BACK_820: return "M5_BACK_820";
    case ACTION_M3_HOME: return "M3_HOME";
    case ACTION_DONE: return "DONE";
    default: return "UNKNOWN";
  }
}
