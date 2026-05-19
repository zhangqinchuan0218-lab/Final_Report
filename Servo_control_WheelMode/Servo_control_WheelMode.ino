#include <SCServo.h>
SMS_STS st;
#define RX 44
#define TX 43
int ID = 1;

// ===== 多圈 =====
int lastPos = 0;
long totalPos = 0;

// ===== PID =====
float Kp = 0.55;
float Kd = 1.5;

long lastError = 0;

// ===== 控制 =====
int tolerance = 5;
int maxSpeed = 3000;
int minSpeed = 120;

// ===== 轨迹队列 =====
struct Motion {
  long delta;   // 位移
};

Motion motionQueue[10];
int motionCount = 0;
int currentMotion = 0;

// ===== 当前目标 =====
long targetPos = 0;

// ===== 状态 =====
bool running = false;

void setup() {
  Serial.begin(115200);
  Serial1.begin(1000000,SERIAL_8N1,RX,TX);
  st.pSerial = &Serial1;

  delay(1000);

  lastPos = st.ReadPos(ID);
  totalPos = 0;

  // ===== 定义轨迹 =====
  addMotion(3 * 4096);     // 正转3圈
  addMotion(-2 * 4096);    // 反转2圈
  addMotion(1 * 4096);     // 正转1圈
  addMotion(-4 * 4096);    // 反转4圈

  // 初始化第一个目标
  targetPos = totalPos + motionQueue[0].delta;
  running = true;

  Serial.println("Start Motion Queue");
}

void loop() {
  updateMultiTurn();

  if(running) {
    if(movePID()) {
      nextMotion();
    }
  }
}

// ================= 多圈 =================
void updateMultiTurn() {
  int pos = st.ReadPos(ID);

  int delta = pos - lastPos;
  if(delta > 2048) delta -= 4096;
  if(delta < -2048) delta += 4096;

  totalPos += delta;
  lastPos = pos;
}

// ================= PID =================
bool movePID() {

  long error = targetPos - totalPos;

  if(abs(error) < tolerance) {
    st.WriteSpe(ID, 0);
    return true;
  }

  long dError = error - lastError;
  lastError = error;

  int speed = Kp * error + Kd * dError;

  if(speed > maxSpeed) speed = maxSpeed;
  if(speed < -maxSpeed) speed = -maxSpeed;

  if(abs(speed) < minSpeed) {
    speed = (speed > 0) ? minSpeed : -minSpeed;
  }

  st.WriteSpe(ID, speed);

  return false;
}

// ================= 队列管理 =================
void addMotion(long delta) {
  motionQueue[motionCount].delta = delta;
  motionCount++;
}

void nextMotion() {
  currentMotion++;

  if(currentMotion >= motionCount) {
    running = false;
    Serial.println("All Done");
    return;
  }

  // 🔥 关键：目标累加
  targetPos += motionQueue[currentMotion].delta;

  Serial.print("Next Motion: ");
  Serial.println(currentMotion);
}