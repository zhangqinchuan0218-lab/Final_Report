#include <Arduino.h>
#include <SCServo.h>

SMS_STS st;

// ============================================================
// 1. 硬件接口配置
// ============================================================
// ESP32 与 ST3215 总线舵机通信使用 Serial1。
// 这里沿用你当前项目的接线：RX=44，TX=43。
#define RX 44
#define TX 43

// 按钮引脚：按下接 GND，使用 INPUT_PULLUP。
// 按钮只用于启动测试序列，不会再写 EEPROM。
#define BUTTON_PIN 46
#define BUTTON_ACTIVE LOW

// 当前测试对象：M4，舵机 ID = 4。
#define M4_ID 4

// ST3215 一圈对应 4096 个位置单位，位置范围通常是 0~4095。
#define ONE_TURN 4096L

// ============================================================
// 2. M4 位置与动作参数
// ============================================================
// 你已经完成 EEPROM 中点校准，所以之后永久中点按 2048 使用。
const int M4_CENTER_POS = 2048;

// 90 度 = 1/4 圈 = 1024 步。
const int M4_90_DEG_STEPS = ONE_TURN / 4;

// 40 度约等于 4096 * 40 / 360 = 455.11 步，取整为 456 步。
// 你要求在原来动作基础上连续加角度，现在总共多 40 度。
const int M4_EXTRA_40_DEG_STEPS = 456;

// 测试动作 1：从中点逆时针旋转 130 度。
// 原来是 90 度，现在总共多 40 度，所以目标 = 2048 - (1024 + 456) = 568。
// 这里假设“位置值减少”为逆时针。
// 如果实测方向相反，把这个目标改成 M4_CENTER_POS + M4_90_DEG_STEPS。
const int M4_CCW_130_TARGET = M4_CENTER_POS - M4_90_DEG_STEPS - M4_EXTRA_40_DEG_STEPS;

// 测试动作 2：顺时针旋转到指定目标 3528。
// 这是你现在指定的顺时针终点，不再按角度自动换算。
const int M4_CW_TARGET = 3528;

// 位置模式运动速度和加速度。
// 如果 M4 动作太猛，可以降低 POSITION_SPEED，例如 500。
const int POSITION_SPEED = 800;
const int POSITION_ACC = 50;

// 到位容差：误差小于 8 个位置单位就认为到位。
const int POSITION_TOLERANCE = 8;

// 每一步到位后停留时间，方便观察动作。
const unsigned long SEQUENCE_DWELL_MS = 1000;

// 单步运动最长等待时间，超过说明可能卡住、掉电或通信失败。
const unsigned long SEQUENCE_MOVE_TIMEOUT_MS = 8000;

// 串口调试打印间隔。
const unsigned long DEBUG_INTERVAL_MS = 500;

// ============================================================
// 3. 运行状态变量
// ============================================================
int targetPos = M4_CENTER_POS;
bool lastButtonState = false;
bool sequenceRunning = false;
int sequenceStep = 0;
unsigned long sequenceStepStartTime = 0;
unsigned long sequenceDwellUntil = 0;
unsigned long lastDebugTime = 0;

// ============================================================
// 4. 函数声明
// ============================================================
void holdCurrentPositionOnStartup();
void handleButtonPress();
void handleSerialCommand();
void startTestSequence();
void runTestSequence();
void advanceSequence();
void moveToTarget(int pos, const char *label);
bool readButtonDebounced();
bool isAtTarget();
void stopAndHoldCurrent();
void printDebug();
bool readFeedback(int &pos, int &speed, int &current, int &load, int &voltage, int &temper);
int circularError(int target, int current);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial1.begin(1000000, SERIAL_8N1, RX, TX);
  st.pSerial = &Serial1;
  delay(1000);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // 上电先保持 M4 当前实际位置，避免程序刚启动就突然转动。
  holdCurrentPositionOnStartup();

  Serial.println();
  Serial.println("========== M4 Rotation Test ==========");
  Serial.println("EEPROM center is already set. No EEPROM write in this sketch.");
  Serial.println("Test sequence: CCW 130 deg, then CW to 3528, then back to center.");
  Serial.println("Targets: center=2048, CCW130=568, CW=3528, final center=2048.");
  Serial.println("Serial commands: t=test, m=center, q=CCW130, w=CW3528, s=stop/hold.");
  Serial.println("Button: start test sequence.");
  Serial.println("======================================");
}

void loop() {
  bool nowPressed = readButtonDebounced();
  if (nowPressed && !lastButtonState) {
    handleButtonPress();
  }
  lastButtonState = nowPressed;

  handleSerialCommand();
  runTestSequence();

  if (millis() - lastDebugTime >= DEBUG_INTERVAL_MS) {
    lastDebugTime = millis();
    printDebug();
  }

  delay(10);
}

void holdCurrentPositionOnStartup() {
  int pos = st.ReadPos(M4_ID);
  if (pos < 0) {
    Serial.println("Startup hold failed: cannot read M4 position.");
    return;
  }

  targetPos = pos;
  sequenceRunning = false;
  moveToTarget(pos, "STARTUP_HOLD_CURRENT");
}

void handleButtonPress() {
  startTestSequence();
}

void handleSerialCommand() {
  while (Serial.available() > 0) {
    char cmd = Serial.read();

    if (cmd == 't' || cmd == 'T') {
      startTestSequence();
    } else if (cmd == 'm' || cmd == 'M') {
      sequenceRunning = false;
      moveToTarget(M4_CENTER_POS, "CENTER_2048");
    } else if (cmd == 'q' || cmd == 'Q') {
      sequenceRunning = false;
      moveToTarget(M4_CCW_130_TARGET, "CCW_130_TARGET");
    } else if (cmd == 'w' || cmd == 'W') {
      sequenceRunning = false;
      moveToTarget(M4_CW_TARGET, "CW_3528_TARGET");
    } else if (cmd == 's' || cmd == 'S') {
      sequenceRunning = false;
      stopAndHoldCurrent();
    }
  }
}

void startTestSequence() {
  // 测试序列只做两步：
  // 第一步：逆时针 130 度，到 568。
  // 第二步：顺时针到你指定的目标 3528。
  // 第三步：回到中点 2048。
  sequenceRunning = true;
  sequenceStep = 0;
  sequenceDwellUntil = 0;
  Serial.println("Start M4 test sequence: CCW 130 deg -> CW 3528 -> center.");
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
    Serial.println("M4 test sequence timeout. Hold current position.");
  }
}

void advanceSequence() {
  sequenceStep++;
  sequenceStepStartTime = millis();

  switch (sequenceStep) {
    case 1:
      moveToTarget(M4_CCW_130_TARGET, "STEP1_CCW_130");
      break;

    case 2:
      moveToTarget(M4_CW_TARGET, "STEP2_CW_3528");
      break;

    case 3:
      moveToTarget(M4_CENTER_POS, "STEP3_BACK_CENTER");
      break;

    default:
      sequenceRunning = false;
      Serial.println("M4 test sequence done.");
      break;
  }
}

void moveToTarget(int pos, const char *label) {
  targetPos = pos;
  st.WritePosEx(M4_ID, targetPos, POSITION_SPEED, POSITION_ACC);

  Serial.print("M4 target ");
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
  int pos = st.ReadPos(M4_ID);
  if (pos < 0) return false;
  return abs(circularError(targetPos, pos)) <= POSITION_TOLERANCE;
}

void stopAndHoldCurrent() {
  int pos = st.ReadPos(M4_ID);
  if (pos >= 0) {
    moveToTarget(pos, "HOLD_CURRENT");
  }
}

void printDebug() {
  int pos = -1;
  int speed = -1;
  int current = -1;
  int load = -1;
  int voltage = -1;
  int temper = -1;
  bool feedbackOk = readFeedback(pos, speed, current, load, voltage, temper);
  bool atTarget = feedbackOk && abs(circularError(targetPos, pos)) <= POSITION_TOLERANCE;

  Serial.print("[M4] target=");
  Serial.print(targetPos);
  Serial.print(" pos=");
  Serial.print(pos);
  Serial.print(" err=");
  Serial.print(feedbackOk ? circularError(targetPos, pos) : 0);
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
  Serial.print(" feedbackOk=");
  Serial.println(feedbackOk);
}

bool readFeedback(int &pos, int &speed, int &current, int &load, int &voltage, int &temper) {
  // 官方推荐 FeedBack(ID) 一次性读回状态缓存，再用 Readxxx(-1) 取缓存值。
  // 比连续发送多条 ReadPos/ReadSpeed/ReadLoad 指令更稳，也更容易判断通信是否正常。
  if (st.FeedBack(M4_ID) == -1) {
    return false;
  }

  pos = st.ReadPos(-1);
  speed = st.ReadSpeed(-1);
  current = st.ReadCurrent(-1);
  load = st.ReadLoad(-1);
  voltage = st.ReadVoltage(-1);
  temper = st.ReadTemper(-1);
  return pos >= 0;
}

int circularError(int target, int current) {
  int error = target - current;
  if (error > 2048) error -= 4096;
  if (error < -2048) error += 4096;
  return error;
}
