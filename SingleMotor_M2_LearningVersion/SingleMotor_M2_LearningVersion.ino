#include <Arduino.h>
#include <SCServo.h>

/*
  ============================================================
  M2 单电机调试程序 - 中文教学注释版
  ============================================================

  这个程序用于单独测试 M2 电机。

  M2 的特点：
  1. 它需要做竖直方向运动。
  2. 上升时要对抗重力，所以比下降更吃力。
  3. 如果控制参数不合适，上升时容易一顿一顿。

  本程序实现的功能：
  1. 按按钮启动。
  2. 电机在 0 点和 1/4 圈位置之间来回运动。
  3. 再按按钮暂停。
  4. 再按按钮继续。
  5. 实时检测电流，超过限制立即停机。
  6. 实时检测堵转，发现电机给了速度但不动，立即停机。
  7. M2 上升和下降使用不同参数：
     - 上升：更大的最小速度 + 重力补偿。
     - 下降：较小速度，防止被重力带着冲过头。

  注意：
  这个程序是“轮式模式”的闭环控制。
  它不是直接调用电机内部的位置模式，而是：
  - 不断读取电机当前编码器位置；
  - 自己累计出 totalPos；
  - 计算 totalPos 和 targetPos 的误差；
  - 根据误差发送速度指令 WriteSpe()。
*/

SMS_STS st;

// ============================================================
// 1. 硬件接口配置
// ============================================================

// ESP32 和总线舵机通信使用 Serial1。
// 这里 RX/TX 要和你的接线保持一致。
#define RX 44
#define TX 43

// 启停按钮引脚。
// 按钮一端接 GPIO46，另一端接 GND。
// 使用 INPUT_PULLUP 后，未按下是 HIGH，按下是 LOW。
#define BUTTON_PIN 46
#define BUTTON_ACTIVE LOW

// 当前测试的是 M2 电机，所以 ID 是 2。
// 如果你想复刻成 M1/M3，只需要改这个 ID 和少量参数。
#define MOTOR_ID 2
#define MOTOR_NAME "M2"

// 总线电机一圈对应 4096 个位置单位。
// 1/4 圈就是 1024。
#define ONE_TURN 4096L

// ============================================================
// 2. 运动范围参数
// ============================================================

// 单电机测试时不要一开始就转太大。
// 这里让 M2 在 home 和 home + 1/4 圈之间来回。
const long TEST_AMPLITUDE = ONE_TURN / 4;

// ============================================================
// 3. 安全保护参数
// ============================================================

// 电流限制。
// 如果 st.ReadCurrent() 读到的电流绝对值超过这个值，就立即停机。
// 具体数值需要根据你的电机、驱动、电源实测调整。
const int CURRENT_LIMIT = 2000;

// 单次运动最长允许时间。
// 如果超过这个时间还没有到达目标，就认为异常，停机。
const unsigned long MOVE_TIMEOUT_MS = 12000;

// 刚开始运动时，电机可能需要一点时间克服静摩擦。
// 这段时间内不判断堵转，避免误判。
const unsigned long STALL_GRACE_MS = 500;

// 堵转需要持续这么久才确认。
// 例如短时间速度为 0 不一定是堵转，可能只是反馈采样瞬间为 0。
const unsigned long STALL_CONFIRM_MS = 1500;

// 判断“速度很低”的阈值。
// 如果已经给了速度指令，但反馈速度长期低于这个值，就可能是堵转。
const int STALL_VELOCITY_LIMIT = 20;

// 接近目标时不要轻易判定堵转。
// 例如离目标只差 100 多个编码单位时，速度低是正常的。
const long WHEEL_STALL_ERROR_LIMIT = 220;

// ============================================================
// 4. 控制参数
// ============================================================

// 最大速度限制，防止电机动作太猛。
const int WHEEL_MAX_SPEED = 600;

// 普通最小速度。
// M2 实际上会使用下面的上行/下行最小速度。
const int WHEEL_MIN_SPEED = 120;

// M2 上行最小速度。
// 你已经确认 target=0 时是向上，所以回到 home 的方向更吃力。
const int M2_UP_MIN_SPEED = 180;

// M2 下行最小速度。
// 下降时有重力帮助，所以速度要保守一些，避免过冲。
const int M2_DOWN_MIN_SPEED = 100;

// 上行重力补偿。
// 当速度方向是上行时，额外增加一点速度指令，让电机持续托住负载。
// 如果上升还是一顿一顿，可以尝试 80、90。
// 如果接近目标容易冲，可以尝试 50、60。
const int M2_UP_GRAVITY_COMP = 70;

// 速度斜坡限制。
// 每次 loop 控制时，速度指令最多变化这么多。
// 作用是让速度变化更平滑，减少忽快忽慢。
const int M2_SPEED_SLEW_STEP = 45;

// 到位误差。
// M2 只要距离目标小于 80 个编码单位，并且速度足够低，就认为到位。
const int WHEEL_TOLERANCE = 80;

// 到达目标后停留多久，再反向运动。
const unsigned long DWELL_MS = 400;

// 串口打印间隔。
const unsigned long DEBUG_INTERVAL_MS = 500;

// ============================================================
// 5. 运行时数据结构
// ============================================================

struct WheelTracker {
  // 上一次读取到的 0-4095 单圈位置。
  int lastPos;

  // 多圈累计位置。
  // 电机实际反馈只有 0-4095，会循环。
  // 我们用 totalPos 把跨圈运动累计起来。
  long totalPos;

  // 目标累计位置。
  long targetPos;

  // 估算出来的速度，单位大约是 编码单位/秒。
  float velocity;

  // 用于计算速度的历史数据。
  long lastTotalPos;
  unsigned long lastVelTime;

  // 上一次发送给电机的速度指令。
  int lastCmdSpeed;
};

WheelTracker motor;

// ============================================================
// 6. 系统状态变量
// ============================================================

// 是否已经启动过。
bool workflowStarted = false;

// 当前是否正在运行。
bool running = false;

// 当前是否暂停。
bool paused = false;

// 是否进入紧急停机。
// 紧急停机后不能靠按钮继续，必须检查硬件并重启 ESP32。
bool emergencyStop = false;

// 按钮上一轮状态，用于检测“按下瞬间”。
bool lastButtonState = false;

// true 表示当前目标是远离 home 的位置。
// false 表示当前目标是回到 home。
bool movingAway = true;

// 当前动作开始时间，用于超时保护。
unsigned long moveStartTime = 0;

// 堵转开始怀疑的时间。
unsigned long stallStartTime = 0;

// 到达目标后的停留结束时间。
unsigned long dwellUntil = 0;

// 上一次串口打印时间。
unsigned long lastDebugTime = 0;

// 启动时的 home 位置。
// M2 会在 wheelHomeTotal 和 wheelHomeTotal + TEST_AMPLITUDE 之间来回。
long wheelHomeTotal = 0;

// ============================================================
// 7. 函数声明
// ============================================================

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

bool checkCurrentProtection();
bool checkStallProtection();
bool readButtonDebounced();

void stopMotor();
void printDebug();

long absLong(long value);
float absFloat(float value);

// ============================================================
// 8. setup：只运行一次
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  // 初始化总线电机串口。
  Serial1.begin(1000000, SERIAL_8N1, RX, TX);
  st.pSerial = &Serial1;
  delay(1000);

  // 按钮使用上拉输入。
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // 读取电机初始位置，并初始化内部变量。
  initMotor();

  Serial.println();
  Serial.println("========== M2 Learning Version ==========");
  Serial.println("按钮：开始 -> 暂停 -> 继续");
  Serial.println("保护：过流 / 堵转 / 超时 会紧急停机");
  Serial.println("运动：home <-> home + 1/4 圈");
  Serial.println("说明：target=0 方向为上升，已加入重力补偿");
  Serial.println("=========================================");
}

// ============================================================
// 9. loop：不断重复运行
// ============================================================

void loop() {
  // 1. 读取按钮，并只在“刚按下”的瞬间触发一次。
  bool nowPressed = readButtonDebounced();
  if (nowPressed && !lastButtonState) {
    handleButtonPress();
  }
  lastButtonState = nowPressed;

  // 2. 更新电机多圈位置和速度。
  updateWheelMotor();

  // 3. 如果系统正在运行，就先做安全检测，再做运动控制。
  if (running && !paused && !emergencyStop) {
    if (checkCurrentProtection() || checkStallProtection()) {
      stopMotor();
    } else {
      runMotion();
    }
  }

  // 4. 定时打印调试信息。
  if (millis() - lastDebugTime >= DEBUG_INTERVAL_MS) {
    lastDebugTime = millis();
    printDebug();
  }

  delay(10);
}

// ============================================================
// 10. 初始化电机
// ============================================================

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
}

// ============================================================
// 11. 按钮状态机
// ============================================================

void handleButtonPress() {
  if (emergencyStop) {
    Serial.println("已经紧急停机。请检查机械结构后重启 ESP32。");
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

  Serial.println("M2 开始测试。");
  setNextTarget();
}

void pauseWorkflow() {
  paused = true;
  running = false;
  stopMotor();
  Serial.println("已暂停。");
}

void resumeWorkflow() {
  paused = false;
  running = true;

  // 继续运行时重置保护计时，避免刚恢复就被之前的时间误判超时。
  moveStartTime = millis();
  stallStartTime = 0;

  Serial.println("继续运行。");
}

void emergencyStopNow(const char *reason) {
  emergencyStop = true;
  running = false;
  paused = false;
  stopMotor();

  Serial.print("紧急停机，原因：");
  Serial.println(reason);
}

// ============================================================
// 12. 多圈位置与速度更新
// ============================================================

void updateWheelMotor() {
  int pos = st.ReadPos(MOTOR_ID);
  if (pos < 0) return;

  // pos 是 0-4095，会循环。
  // 比如从 4090 到 5，其实是正向走了 11，而不是 -4085。
  int delta = pos - motor.lastPos;
  if (delta > 2048) delta -= 4096;
  if (delta < -2048) delta += 4096;

  motor.totalPos += delta;
  motor.lastPos = pos;

  // 每 20ms 左右估算一次速度。
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

// ============================================================
// 13. 目标切换
// ============================================================

void setNextTarget() {
  moveStartTime = millis();
  stallStartTime = 0;

  if (movingAway) {
    motor.targetPos = wheelHomeTotal + TEST_AMPLITUDE;
  } else {
    motor.targetPos = wheelHomeTotal;
  }

  Serial.print("新的目标 total=");
  Serial.println(motor.targetPos);
}

void runMotion() {
  // 到达目标后，先停留一小会儿，再换方向。
  if (dwellUntil > 0) {
    if (millis() >= dwellUntil) {
      dwellUntil = 0;
      movingAway = !movingAway;
      setNextTarget();
    }
    return;
  }

  bool done = controlWheelToTarget();
  if (done) {
    stopMotor();
    Serial.println("M2 到达目标。");
    dwellUntil = millis() + DWELL_MS;
  }
}

// ============================================================
// 14. 核心控制：根据误差计算速度
// ============================================================

bool controlWheelToTarget() {
  long error = motor.targetPos - motor.totalPos;

  // 如果误差足够小，并且速度也已经很低，就认为到位。
  if (absLong(error) < WHEEL_TOLERANCE && absFloat(motor.velocity) < 60) {
    motor.lastCmdSpeed = 0;
    st.WriteSpe(MOTOR_ID, 0);
    return true;
  }

  /*
    简化版 PD 控制：

    output = Kp * error - Kv * velocity

    error 越大，速度指令越大。
    velocity 越大，速度指令会被压低，避免冲过头。

    M2 上升之前一顿一顿，主要是 Kv 太强时：
    电机刚动起来，velocity 变大，控制器马上把速度压低，
    然后又被重力/摩擦拖住，于是形成顿挫。

    所以这里把 Kv 调小为 0.45。
  */
  float output = 1.0f * error - 0.45f * motor.velocity;

  // 接近目标时稍微降速。
  if (absLong(error) < 500) {
    output *= 0.7f;
  }

  int speed = (int)output;

  // 限制最大速度。
  if (speed > WHEEL_MAX_SPEED) speed = WHEEL_MAX_SPEED;
  if (speed < -WHEEL_MAX_SPEED) speed = -WHEEL_MAX_SPEED;

  // M2 上下方向不同参数。
  // 你确认 target=0 时向上，因此通常 speed < 0 是上行方向。
  int directionMinSpeed = speed < 0 ? M2_UP_MIN_SPEED : M2_DOWN_MIN_SPEED;

  if (speed > 0 && speed < directionMinSpeed) speed = directionMinSpeed;
  if (speed < 0 && speed > -directionMinSpeed) speed = -directionMinSpeed;

  // 上行方向额外加重力补偿。
  if (speed < 0) {
    speed -= M2_UP_GRAVITY_COMP;
    if (speed < -WHEEL_MAX_SPEED) speed = -WHEEL_MAX_SPEED;
  }

  // 速度斜坡限制：不要让指令突然变化太多。
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

// ============================================================
// 15. 电流保护
// ============================================================

bool checkCurrentProtection() {
  int current = st.ReadCurrent(MOTOR_ID);

  // 有些库读取失败会返回 -1。
  // 读取成功并超过阈值时，立即紧急停机。
  if (current != -1 && abs(current) > CURRENT_LIMIT) {
    Serial.print("电流过大 current=");
    Serial.println(current);
    emergencyStopNow("over current");
    return true;
  }

  return false;
}

// ============================================================
// 16. 堵转与超时保护
// ============================================================

bool checkStallProtection() {
  if (moveStartTime == 0 || dwellUntil > 0) return false;

  unsigned long now = millis();

  // 超时保护。
  if (now - moveStartTime > MOVE_TIMEOUT_MS) {
    emergencyStopNow("move timeout");
    return true;
  }

  // 启动初期不给堵转判断。
  if (now - moveStartTime < STALL_GRACE_MS) {
    return false;
  }

  long error = motor.targetPos - motor.totalPos;

  // 离目标很近时，不用低速来判断堵转。
  // 因为接近目标时低速是正常现象。
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

// ============================================================
// 17. 按钮消抖
// ============================================================

bool readButtonDebounced() {
  static bool stableState = false;
  static bool lastRawState = false;
  static unsigned long lastChangeTime = 0;

  bool rawState = (digitalRead(BUTTON_PIN) == BUTTON_ACTIVE);

  if (rawState != lastRawState) {
    lastRawState = rawState;
    lastChangeTime = millis();
  }

  // 状态稳定 30ms 后，才认为按钮状态改变。
  if (millis() - lastChangeTime >= 30) {
    stableState = rawState;
  }

  return stableState;
}

// ============================================================
// 18. 停止电机
// ============================================================

void stopMotor() {
  st.WriteSpe(MOTOR_ID, 0);
  motor.lastCmdSpeed = 0;
}

// ============================================================
// 19. 串口调试输出
// ============================================================

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

// ============================================================
// 20. 小工具函数
// ============================================================

long absLong(long value) {
  return value < 0 ? -value : value;
}

float absFloat(float value) {
  return value < 0 ? -value : value;
}
