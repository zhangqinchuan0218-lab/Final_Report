#include <SCServo.h>
SMS_STS st;

#define RX 44
#define TX 43

// ================= 电机结构 =================
struct Motor {
  int id;
  int lastPos;
  long totalPos;
  float velocity;
  long lastTotalPos;
  unsigned long lastVelTime;
  long targetPos;
  float Kp;
  float Kv;
  bool done;
};

// 两个电机
Motor m1, m2;

// ================= 控制参数 =================
int tolerance = 5;
int maxSpeed = 3000;
int minSpeed = 120;

// ================= 协同模式 =================
// 0 = 同时完成
// 1 = 交替完成
// 2 = 反向同步
int mode = 0;

// ================= 队列 =================
struct Motion {
  long delta1;
  long delta2;
};

Motion queue[20];
int motionCount = 0;
int currentMotion = 0;

// ================= 状态 =================
bool running = false;

// ================= 初始化 =================
void initMotor(Motor &m, int id, float kp, float kv) {
  m.id = id;
  m.lastPos = st.ReadPos(id);
  m.totalPos = 0;
  m.velocity = 0;
  m.lastTotalPos = 0;
  m.lastVelTime = 0;
  m.Kp = kp;
  m.Kv = kv;
  m.done = false;
}

// ================= setup =================
void setup() {
  Serial.begin(115200);
  Serial1.begin(1000000, SERIAL_8N1, RX, TX);
  st.pSerial = &Serial1;

  delay(1000);

  initMotor(m1, 1, 1.95, 0.725);
  initMotor(m2, 2, 1.95, 0.725);

  // ===== 定义协同轨迹 =====
  addMotion(3*4096, 3*4096);    
  addMotion(-2*4096, 2*4096);   
  addMotion(1*4096, 0);         
  addMotion(0, -2*4096);        
  addMotion(3.5*4096, -3.5*4096);  
  addMotion(-3.5*4096, 0);
  addMotion(0, 3.5*4096);         

  setNextTarget();
  running = true;

  Serial.println("Dual Motor Sync Start");
}

// ================= loop =================
void loop() {
  updateMotor(m1);
  updateMotor(m2);

  if (running) {
    // 控制
    m1.done = controlMotor(m1);
    m2.done = controlMotor(m2);

    // 协同逻辑
    runCoordination();
  }
}

// ================= 多圈更新 =================
void updateMotor(Motor &m) {
  int pos = st.ReadPos(m.id);
  int delta = pos - m.lastPos;
  if(delta > 2048) delta -= 4096;
  if(delta < -2048) delta += 4096;
  m.totalPos += delta;
  m.lastPos = pos;

  unsigned long now = millis();
  if (now - m.lastVelTime >= 20) {
    m.velocity = (m.totalPos - m.lastTotalPos)/0.02;
    m.lastTotalPos = m.totalPos;
    m.lastVelTime = now;
  }
}

// ================= PID控制 =================
bool controlMotor(Motor &m) {
  long error = m.targetPos - m.totalPos;

  if (abs(error) < tolerance && abs(m.velocity) < 50) {
    st.WriteSpe(m.id, 0);
    return true;
  }

  float output = m.Kp * error - m.Kv * m.velocity;

  if(abs(error) < 300) output *= 0.5; // 减速区

  int speed = (int)output;
  if(speed > maxSpeed) speed = maxSpeed;
  if(speed < -maxSpeed) speed = -maxSpeed;
  if(abs(speed) < minSpeed) speed = (speed>0)? minSpeed:-minSpeed;

  st.WriteSpe(m.id, speed);
  return false;
}

// ================= 协同逻辑 =================
void runCoordination() {
  switch(mode){
    case 0: // 同时完成
      if(m1.done && m2.done) nextMotion();
      break;

    case 1: // 交替完成
      if((m1.done && !m2.done) || (!m1.done && m2.done)) return;
      if(m1.done && m2.done) nextMotion();
      break;

    case 2: // 反向同步
      if(m1.done && m2.done) nextMotion();
      break;
  }
}

// ================= 队列管理 =================
void addMotion(long d1, long d2) {
  if(motionCount >= 20) return;
  queue[motionCount].delta1 = d1;
  queue[motionCount].delta2 = d2;
  motionCount++;
}

void setNextTarget() {
  if(currentMotion >= motionCount) {
    running = false;
    Serial.println("All Done");
    return;
  }

  m1.targetPos += queue[currentMotion].delta1;
  m2.targetPos += queue[currentMotion].delta2;

  Serial.print("Motion Step: "); Serial.println(currentMotion);
}

void nextMotion() {
  currentMotion++;
  setNextTarget();
}

