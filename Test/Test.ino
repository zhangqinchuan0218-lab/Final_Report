#include <dummy.h>

#include <SCServo.h>
SMS_STS st;

#define RX 44
#define TX 43

// ================= 按钮 =================
// 按钮一端接 BUTTON_PIN，另一端接 GND
// 使用 INPUT_PULLUP：按下 = LOW，松开 = HIGH
#define BUTTON_PIN 45
#define BUTTON_ACTIVE LOW

// ================= 电机结构 =================
struct Motor {
  int id;

  // 轮子模式
  int lastPos;
  long totalPos;
  float velocity;
  long lastTotalPos;
  unsigned long lastVelTime;
  long targetPos;   // 轮子模式目标

  float Kp;
  float Kv;

  // 位置模式
  int targetPos2;   // 位置模式目标

  bool done;
  bool isWheel;     // true = 轮子模式, false = 位置模式
};

// 两个电机
Motor m1, m2;

// ================= 控制参数 =================
int tolerance = 5;     // 轮子模式误差容忍
int maxSpeed = 3000;   // 轮子模式最大速度
int minSpeed = 120;    // 轮子模式最小速度

// ================= 协同模式 =================
// 0 = 同时完成
// 1 = 交替完成
int mode = 0;

// ================= 队列 =================
struct Motion {
  long deltaWheel;   // 电机1轮子模式增量
  int  absPos;       // 电机2位置模式绝对位置
};

Motion queue[20];
int motionCount = 0;
int currentMotion = 0;

// ================= 状态 =================
bool running = false;
bool targetLoaded = false;
bool lastButtonPressed = false;

// ================= 初始化 =================
void initMotor(Motor &m, int id, bool wheel, float kp=0, float kv=0) {
  m.id = id;
  m.isWheel = wheel;

  if (wheel) {
    m.lastPos = st.ReadPos(id);
    m.totalPos = 0;
    m.velocity = 0;
    m.lastTotalPos = 0;
    m.lastVelTime = millis();
    m.Kp = kp;
    m.Kv = kv;
    m.targetPos = 0;
  } else {
    // 位置模式直接用实际当前位置作为初始目标
    m.targetPos2 = st.ReadPos(id);
  }

  m.done = false;
}

// ================= setup =================
void setup() {
  Serial.begin(115200);
  Serial1.begin(1000000, SERIAL_8N1, RX, TX);
  st.pSerial = &Serial1;
  delay(1000);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // 初始化电机
  initMotor(m1, 1, true, 1.95, 0.75);   // 轮子模式
  initMotor(m2, 2, false);              // 位置模式

  // ===== 队列动作示例 =====
  addMotion(3 * 4096, 2048);   // 电机1轮子模式正转3圈，电机2绝对位置2048
  addMotion(-2 * 4096, 1024);  // 电机1反转2圈，电机2绝对位置1024
  addMotion(1 * 4096, 4096);   // 电机1正转1圈，电机2绝对位置4096
  addMotion(0, 0);             // 电机1停，电机2绝对位置0

  Serial.println("Dual Motor Mixed Mode Ready");
  Serial.println("Hold button to run, release button to stop");
}

// ================= loop =================
void loop() {
  bool buttonPressed = readButtonPressed();

  // 松开按钮：立即停止，不再执行队列控制
  if (!buttonPressed) {
    if (running || lastButtonPressed) {
      stopAllMotors();
      Serial.println("Button released, stop");
    }

    running = false;
    lastButtonPressed = false;
    return;
  }

  // 按钮刚按下：启动或继续队列
  if (!lastButtonPressed) {
    startQueue();
    lastButtonPressed = true;
  }

  if (!running) return;

  // 轮子模式更新
  if (m1.isWheel) updateMotor(m1);
  m1.done = m1.isWheel ? controlWheelMotor(m1) : controlPositionMotor(m1);

  // 位置模式更新
  if (!m2.isWheel) m2.done = controlPositionMotor(m2);

  // 协同逻辑
  runCoordination();
}

// ================= 按钮消抖 =================
bool readButtonPressed() {
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

// ================= 启动 / 继续队列 =================
void startQueue() {
  if (motionCount <= 0) return;

  // 如果上一次已经全部完成，再次按下时从当前位置重新开始整套队列
  if (currentMotion >= motionCount) {
    currentMotion = 0;

    // 轮子模式以当前位置作为新的起点，避免重新追旧目标
    updateMotor(m1);
    m1.targetPos = m1.totalPos;

    targetLoaded = false;
  }

  // 第一次启动，或者刚进入下一步时，加载当前目标
  if (!targetLoaded) {
    setNextTarget();
  }

  running = true;
  Serial.println("Button pressed, run");
}

// ================= 停止所有电机 =================
void stopAllMotors() {
  // 轮子模式立即停
  st.WriteSpe(m1.id, 0);

  // 位置模式保持当前位置，避免松手后继续跑向原目标
  int posNow = st.ReadPos(m2.id);
  st.WritePosEx(m2.id, posNow, 0, 50);

  m1.done = false;
  m2.done = false;
}

// ================= 多圈更新 =================
void updateMotor(Motor &m) {
  int pos = st.ReadPos(m.id);
  int delta = pos - m.lastPos;

  if (delta > 2048) delta -= 4096;
  if (delta < -2048) delta += 4096;

  m.totalPos += delta;
  m.lastPos = pos;

  unsigned long now = millis();
  if (now - m.lastVelTime >= 20) {
    float dt = (now - m.lastVelTime) / 1000.0;
    m.velocity = (m.totalPos - m.lastTotalPos) / dt;
    m.lastTotalPos = m.totalPos;
    m.lastVelTime = now;
  }
}

// ================= PID控制轮子模式 =================
bool controlWheelMotor(Motor &m) {
  long error = m.targetPos - m.totalPos;

  if (abs(error) < tolerance && abs(m.velocity) < 50) {
    st.WriteSpe(m.id, 0);
    return true;
  }

  float output = m.Kp * error - m.Kv * m.velocity;

  // 减速区
  if (abs(error) < 300) output *= 0.5;

  int speed = (int)output;

  if (speed > maxSpeed) speed = maxSpeed;
  if (speed < -maxSpeed) speed = -maxSpeed;

  if (abs(speed) < minSpeed) {
    speed = (speed > 0) ? minSpeed : -minSpeed;
  }

  st.WriteSpe(m.id, speed);
  return false;
}

// ================= 位置模式控制 =================
bool controlPositionMotor(Motor &m) {
  int posNow = st.ReadPos(m.id);

  if (abs(posNow - m.targetPos2) < 5) {
    return true;
  }

  st.WritePosEx(m.id, m.targetPos2, 2000, 50);
  return false;
}

// ================= 协同逻辑 =================
void runCoordination() {
  switch (mode) {
    case 0: // 同时完成
      if (m1.done && m2.done) nextMotion();
      break;

    case 1: // 交替完成
      if ((m1.done && !m2.done) || (!m1.done && m2.done)) return;
      if (m1.done && m2.done) nextMotion();
      break;
  }
}

// ================= 队列管理 =================
void addMotion(long wheelDelta, int absPos) {
  if (motionCount >= 20) return;

  queue[motionCount].deltaWheel = wheelDelta;
  queue[motionCount].absPos = absPos;
  motionCount++;
}

void setNextTarget() {
  if (currentMotion >= motionCount) {
    running = false;
    targetLoaded = false;
    Serial.println("All Done");
    return;
  }

  // 电机1轮子模式累加
  m1.targetPos += queue[currentMotion].deltaWheel;

  // 电机2位置模式直接设置绝对目标
  m2.targetPos2 = queue[currentMotion].absPos;

  m1.done = false;
  m2.done = false;
  targetLoaded = true;

  Serial.print("Motion Step: ");
  Serial.println(currentMotion);
}

void nextMotion() {
  currentMotion++;
  targetLoaded = false;
  setNextTarget();
}
