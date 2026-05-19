#include <SCServo.h>
SMS_STS st;
 // 要测试的舵机ID
 #define RX 44
 #define TX 43
 int ID=3;
 int ModeType=1;//1是轮子模式，0是位置模式

void setup() {
  Serial.begin(115200);
  Serial1.begin(1000000, SERIAL_8N1, RX, TX); // 自定义串口
  st.pSerial = &Serial1;

  // put your setup code here, to run once:

}

void loop() {
  st.unLockEprom(ID);
  st.writeByte(ID, SMS_STS_MODE, ModeType); 
  st.writeWord(ID, 11, 0); 
  st.LockEprom(ID);
 int mode = st.readByte(ID, SMS_STS_MODE);

  Serial.print("Current Mode: ");
  Serial.println(mode);
  // put your main code here, to run repeatedly:

}
