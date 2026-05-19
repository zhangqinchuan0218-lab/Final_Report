#include <Arduino.h>
#include <SCServo.h>

SCSCL sc;

// ============================================================
// 1. 硬件接口配置
// ============================================================
// M5 是 SC09，所以必须使用 SCSCL 协议。
// 当前接线沿用你的 ESP32 项目：
// 舵机 TX -> ESP32 RX44
// 舵机 RX -> ESP32 TX43
#define RX 44
#define TX 43

// 物理按钮：按钮一端接 GPIO46，另一端接 GND。
// 使用 INPUT_PULLUP，所以按下时为 LOW。
#define BUTTON_PIN 46
#define BUTTON_ACTIVE LOW

// ============================================================
// 2. ID 修改参数
// ============================================================
// 目标：把 M5 的 SC09 ID 从 1 改成 5。
// 重要：执行这个程序时，总线上最好只接这一只需要改 ID 的 SC09。
// 如果总线上有多个 ID=1 的舵机，它们可能会一起被改成 5。
const int OLD_ID = 1;
const int NEW_ID = 5;

// 扫描 ID 范围。
// SC09 常见 ID 一般在 1~253，这里为了串口输出不刷太多，默认扫 1~20。
// 如果你怀疑 ID 被改到更大的数，可以把 SCAN_ID_END 改到 253。
const int SCAN_ID_START = 1;
const int SCAN_ID_END = 20;

// 串口调试输出间隔。
const unsigned long DEBUG_INTERVAL_MS = 1000;

// ============================================================
// 3. 运行状态变量
// ============================================================
bool lastButtonState = false;
bool idChangeAttempted = false;
unsigned long lastDebugTime = 0;

// ============================================================
// 4. 函数声明
// ============================================================
bool readButtonDebounced();
void handleButtonPress();
bool changeIdFrom1To5();
bool servoResponds(int id);
int readServoIdRegister(int id);
void scanAndPrintIds(const char *label);
void printStatus();

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial1.begin(1000000, SERIAL_8N1, RX, TX);
  sc.pSerial = &Serial1;
  delay(1000);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Serial.println();
  Serial.println("========== M5 SC09 ID Change ==========");
  Serial.println("Target: change SC09 ID from 1 to 5.");
  Serial.println("Button only: press physical button once to write EEPROM ID.");
  Serial.println("Before pressing, connect ONLY the SC09 that should become M5.");
  Serial.println("This uses SCSCL: unLockEprom(1) -> writeByte(1, SCSCL_ID, 5) -> LockEprom(5).");
  Serial.println("The sketch scans responding IDs and reads SCSCL_ID register for confirmation.");
  Serial.println("========================================");

  scanAndPrintIds("Boot scan");
}

void loop() {
  bool nowPressed = readButtonDebounced();
  if (nowPressed && !lastButtonState) {
    handleButtonPress();
  }
  lastButtonState = nowPressed;

  if (millis() - lastDebugTime >= DEBUG_INTERVAL_MS) {
    lastDebugTime = millis();
    printStatus();
  }

  delay(10);
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

void handleButtonPress() {
  if (idChangeAttempted) {
    Serial.println("ID change was already attempted in this boot. Restart ESP32 if you need to run again.");
    return;
  }

  idChangeAttempted = true;
  changeIdFrom1To5();
}

bool changeIdFrom1To5() {
  Serial.println();
  Serial.println("Start changing M5 SC09 ID: 1 -> 5");
  scanAndPrintIds("Before write scan");

  bool oldResponds = servoResponds(OLD_ID);
  bool newResponds = servoResponds(NEW_ID);

  Serial.print("Before write: ID 1 responds = ");
  Serial.print(oldResponds);
  Serial.print(", ID 5 responds = ");
  Serial.println(newResponds);

  if (!oldResponds && newResponds) {
    Serial.println("It looks like the servo is already ID 5. No EEPROM write needed.");
    return true;
  }

  if (!oldResponds) {
    Serial.println("Cannot find servo at ID 1. ID change aborted.");
    return false;
  }

  // 官方 SCSCL ProgramEprom 示例的写法：
  // 1. 解锁旧 ID 的 EEPROM 保护；
  // 2. 向 ID 地址写入新 ID；
  // 3. 用新 ID 重新锁定 EEPROM。
  int unlockResult = sc.unLockEprom(OLD_ID);
  delay(100);

  int writeResult = sc.writeByte(OLD_ID, SCSCL_ID, NEW_ID);
  delay(300);

  int lockResult = sc.LockEprom(NEW_ID);
  delay(500);

  Serial.print("unLockEprom result = ");
  Serial.println(unlockResult);
  Serial.print("writeByte ID result = ");
  Serial.println(writeResult);
  Serial.print("LockEprom result = ");
  Serial.println(lockResult);

  bool oldAfter = servoResponds(OLD_ID);
  bool newAfter = servoResponds(NEW_ID);
  scanAndPrintIds("After write scan");

  Serial.print("After write: ID 1 responds = ");
  Serial.print(oldAfter);
  Serial.print(", ID 5 responds = ");
  Serial.println(newAfter);

  if (!oldAfter && newAfter) {
    Serial.println("SUCCESS: M5 SC09 ID changed from 1 to 5.");
    return true;
  }

  if (oldAfter && !newAfter) {
    Serial.println("FAILED: servo still responds at ID 1, not ID 5.");
    return false;
  }

  if (oldAfter && newAfter) {
    Serial.println("WARNING: both ID 1 and ID 5 respond. Check whether more than one servo is connected.");
    return false;
  }

  Serial.println("FAILED: neither ID 1 nor ID 5 responds. Check power, wiring, and baud rate.");
  return false;
}

bool servoResponds(int id) {
  // 用读取位置来判断该 ID 是否在线。
  // SC09 位置范围是 0~1023，读取失败会返回 -1。
  int pos = sc.ReadPos(id);
  return pos >= 0;
}

int readServoIdRegister(int id) {
  // 直接读取该舵机 EEPROM 里的 ID 地址。
  // 注意：要读取 ID 寄存器，仍然需要先知道/扫描到它当前响应的通信 ID。
  // 读取成功时应返回它自己的 ID；失败返回 -1。
  return sc.readByte(id, SCSCL_ID);
}

void scanAndPrintIds(const char *label) {
  Serial.println();
  Serial.print("[");
  Serial.print(label);
  Serial.println("]");

  bool foundAny = false;

  for (int id = SCAN_ID_START; id <= SCAN_ID_END; id++) {
    int pos = sc.ReadPos(id);
    delay(8);

    if (pos >= 0) {
      foundAny = true;
      int idReg = readServoIdRegister(id);
      delay(8);

      Serial.print("Found response at ID ");
      Serial.print(id);
      Serial.print(" | EEPROM SCSCL_ID=");
      Serial.print(idReg);
      Serial.print(" | pos=");
      Serial.println(pos);
    }
  }

  if (!foundAny) {
    Serial.print("No SC09 response found in ID range ");
    Serial.print(SCAN_ID_START);
    Serial.print("~");
    Serial.println(SCAN_ID_END);
  }

  Serial.println();
}

void printStatus() {
  int pos1 = sc.ReadPos(OLD_ID);
  delay(5);
  int pos5 = sc.ReadPos(NEW_ID);
  delay(5);
  int idReg1 = (pos1 >= 0) ? readServoIdRegister(OLD_ID) : -1;
  delay(5);
  int idReg5 = (pos5 >= 0) ? readServoIdRegister(NEW_ID) : -1;

  Serial.print("[SC09 ID CHECK] ID1 pos=");
  Serial.print(pos1);
  Serial.print(" idReg=");
  Serial.print(idReg1);
  Serial.print(" ID5 pos=");
  Serial.print(pos5);
  Serial.print(" idReg=");
  Serial.print(idReg5);
  Serial.print(" attempted=");
  Serial.println(idChangeAttempted);
}
