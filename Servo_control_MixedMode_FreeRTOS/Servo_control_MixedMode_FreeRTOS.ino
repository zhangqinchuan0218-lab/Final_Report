#include <Arduino.h>
#include <SCServo.h>

SMS_STS st;

#define RX 44
#define TX 43

// ================= 传感器 =================
#define SENSOR_PIN 45

// 如果传感器触发时输出低电平，就保持 LOW
// 如果触发时输出高电平，就改成 HIGH
#define SENSOR_ACTIVE LOW

// ================= 任务句柄 =================
TaskHandle_t sensorTaskHandle = NULL;
TaskHandle_t motorTaskHandle  = NULL;
TaskHandle_t debugTaskHandle  = NULL;

// ================= 电机结构 =================
struct Motor {
  int id;

  // 轮子模式
  int lastPos;
  long totalPos;
  float velocity;
  long lastTotalPos;
  unsigned long lastVelTime;
  long targetPos;

  float Kp;
  float Kv;

  // 位置模式
  int targetPos2;

  bool done;
  bool isWheel;
};

// 两个电机
Motor m1, m2;

// ================= 控制参数 =================
int tolerance = 20;
int maxSpeed = 3000;
int minSpeed = 180;

// ================= 协同模式 =================
// 0 = 同时完成
// 1 = 交替完成（先保留你原来的逻辑）
int mode = 0;

// ================= 队列 =================
struct Motion {
  long deltaWheel;   // 电机1轮子模式增量
  int  absPos;       // 电机2位置模式绝对位置
};

Motion motionQueue[20];
int motionCount = 0;
int currentMotion = 0;

// ================= 系统状态 =================
volatile bool sensorTriggered = false;   // 传感器是否触发
bool targetLoaded = false;

// ================= 函数声明 =================
void initMotor(Motor &m, int id, bool wheel, float kp = 0, float kv = 0);

bool readSensorTriggered();

void stopWheelMotor();
void updateMotor(Motor &m);
bool controlWheelMotor(Motor &m);
bool controlPositionMotor(Motor &m);

void runCoordination();

void addMotion(long wheelDelta, int absPos);
void setNextTarget();
void nextMotion();

// FreeRTOS任务
void taskSensor(void *pvParameters);
void taskMotor(void *pvParameters);
void taskDebug(void *pvParameters);

// ================= 初始化单个电机 =================
void initMotor(Motor &m, int id, bool wheel, float kp, float kv) {
  m.id = id;
  m.isWheel = wheel;

  if (wheel) {
    int pos = st.ReadPos(id);
    if (pos < 0) pos = 0;
    m.lastPos = pos;
    m.totalPos = 0;
    m.velocity = 0;
    m.lastTotalPos = 0;
    m.lastVelTime = millis();
    m.Kp = kp;
    m.Kv = kv;
    m.targetPos = 0;
  } else {
    int pos = st.ReadPos(id);
    if (pos < 0) pos = 0;
    m.targetPos2 = pos;
  }

  m.done = false;
}

// ================= setup =================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial1.begin(1000000, SERIAL_8N1, RX, TX);
  st.pSerial = &Serial1;
  delay(1000);

  pinMode(SENSOR_PIN, INPUT_PULLUP);

  // 初始化两个电机
  initMotor(m1, 1, true, 1.95, 0.9);   // m1：轮子模式
  initMotor(m2, 4, false);              // m2：位置模式

  // ===== 动作队列 =====
  addMotion(1 * 4096, 0);
  addMotion(-1 * 4096, 4096);
 addMotion(1 * 4096, 0);
  addMotion(-1 * 4096, 4096);
   addMotion(1 * 4096, 0);
  addMotion(-1 * 4096, 4096);
  addMotion(1 * 4096, 0);
  addMotion(-1 * 4096, 4096);
  addMotion(1 * 4096, 0);
  addMotion(-1 * 4096, 4096);
  addMotion(1 * 4096, 0);
  addMotion(-1 * 4096, 4096);
  addMotion(1 * 4096, 0);
  addMotion(-1 * 4096, 4096);
  addMotion(1 * 4096, 0);
  addMotion(-1 * 4096, 4096);
  addMotion(1 * 4096, 0);
  addMotion(-1 * 4096, 4096);

  // 上电直接开始第一步
  setNextTarget();

  Serial.println("FreeRTOS Dual Motor Ready");
  Serial.println("Power on -> both motors run");
  Serial.println("Sensor triggered -> wheel motor pause only");
  Serial.println("Sensor released -> wheel motor resume");

  // 传感器任务
  xTaskCreatePinnedToCore(
    taskSensor,
    "SensorTask",
    2048,
    NULL,
    3,
    &sensorTaskHandle,
    0
  );

  // 电机任务
  xTaskCreatePinnedToCore(
    taskMotor,
    "MotorTask",
    4096,
    NULL,
    4,
    &motorTaskHandle,
    1
  );

  // 调试任务
  xTaskCreatePinnedToCore(
    taskDebug,
    "DebugTask",
    2048,
    NULL,
    1,
    &debugTaskHandle,
    1
  );
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}

// ================= 传感器消抖读取 =================
bool readSensorTriggered() {
  static bool stableState = false;
  static bool lastRawState = false;
  static unsigned long lastChangeTime = 0;

  bool rawState = (digitalRead(SENSOR_PIN) == SENSOR_ACTIVE);

  if (rawState != lastRawState) {
    lastRawState = rawState;
    lastChangeTime = millis();
  }

  if (millis() - lastChangeTime >= 20) {
    stableState = rawState;
  }

  return stableState;
}

// ================= 传感器任务 =================
void taskSensor(void *pvParameters) {
  while (1) {
    sensorTriggered = readSensorTriggered();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ================= 电机任务 =================
void taskMotor(void *pvParameters) {
  bool lastSensorState = false;

  while (1) {
    // ===== m1：轮子模式 =====
    if (m1.isWheel) {
      updateMotor(m1);

      // 传感器触发 -> m1暂停
      if (sensorTriggered) {
        stopWheelMotor();
        m1.done = false;   // 当前步未完成，等待恢复后继续
      } else {
        // 传感器未触发 -> m1继续执行当前目标
        m1.done = controlWheelMotor(m1);
      }
    } else {
      m1.done = controlPositionMotor(m1);
    }

    // ===== m2：位置模式一直继续执行，不受传感器影响 =====
    if (m2.isWheel) {
      updateMotor(m2);
      m2.done = controlWheelMotor(m2);
    } else {
      m2.done = controlPositionMotor(m2);
    }

    // ===== 传感器状态提示 =====
    if (sensorTriggered && !lastSensorState) {
      Serial.println("Sensor triggered -> m1 pause, m2 keep running");
    }

    if (!sensorTriggered && lastSensorState) {
      Serial.println("Sensor released -> m1 resume");
    }

    lastSensorState = sensorTriggered;

    // ===== 协同逻辑 =====
    // 只有两个电机都完成，才进入下一步
    runCoordination();

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ================= 调试任务 =================
void taskDebug(void *pvParameters) {
  while (1) {
    Serial.print("[Sensor] ");
    Serial.print(sensorTriggered);

    Serial.print(" | [STEP] ");
    Serial.print(currentMotion);
    Serial.print("/");
    Serial.print(motionCount);

    Serial.print(" | [M1 total] ");
    Serial.print(m1.totalPos);

    Serial.print(" | [M1 target] ");
    Serial.print(m1.targetPos);

    Serial.print(" | [M1 vel] ");
    Serial.print(m1.velocity);

    Serial.print(" | [M1 done] ");
    Serial.print(m1.done);

    Serial.print(" | [M2 target] ");
    Serial.print(m2.targetPos2);

    Serial.print(" | [M2 done] ");
    Serial.println(m2.done);

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

// ================= 只停止轮子模式电机 =================
void stopWheelMotor() {
  st.WriteSpe(m1.id, 0);
}

// ================= 多圈更新 =================
void updateMotor(Motor &m) {
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

// ================= 轮子模式控制 =================
bool controlWheelMotor(Motor &m) {
  long error = m.targetPos - m.totalPos;

  if (abs(error) < tolerance && abs(m.velocity) < 50) {
    st.WriteSpe(m.id, 0);
    return true;
  }

  float output = m.Kp * error - m.Kv * m.velocity;

  if (abs(error) < 400) {
    output *= 0.5f;
  }

  int speed = (int)output;

  if (speed > maxSpeed) speed = maxSpeed;
  if (speed < -maxSpeed) speed = -maxSpeed;

  if (speed > 0 && speed < minSpeed) speed = minSpeed;
  if (speed < 0 && speed > -minSpeed) speed = -minSpeed;

  st.WriteSpe(m.id, speed);
  return false;
}

// ================= 位置模式控制 =================
bool controlPositionMotor(Motor &m) {
  int posNow = st.ReadPos(m.id);
  if (posNow < 0) return false;

  if (abs(posNow - m.targetPos2) < 5) {
    return true;
  }

  st.WritePosEx(m.id, m.targetPos2, 2000, 50);
  return false;
}

// ================= 协同逻辑 =================
void runCoordination() {
  switch (mode) {
    case 0:   // 同时完成
      if (m1.done && m2.done) {
        nextMotion();
      }
      break;

    case 1:   // 保留原来的写法
      if ((m1.done && !m2.done) || (!m1.done && m2.done)) {
        return;
      }
      if (m1.done && m2.done) {
        nextMotion();
      }
      break;
  }
}

// ================= 队列管理 =================
void addMotion(long wheelDelta, int absPos) {
  if (motionCount >= 20) return;

  motionQueue[motionCount].deltaWheel = wheelDelta;
  motionQueue[motionCount].absPos = absPos;
  motionCount++;
}

void setNextTarget() {
  if (currentMotion >= motionCount) {
    Serial.println("All Done");
    stopWheelMotor();

    // 位置模式保持当前最后目标，不再继续改
    return;
  }

  // m1：轮子模式累加目标
  m1.targetPos += motionQueue[currentMotion].deltaWheel;

  // m2：位置模式绝对目标
  m2.targetPos2 = motionQueue[currentMotion].absPos;

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