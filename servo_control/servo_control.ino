#include <SCServo.h>
SMS_STS st;
 // 要测试的舵机ID
 #define S_U0RXD 44
 #define S_U0TXD 43

 byte ID[2];
 s16 Position[2];
 u16 Speed[2];
 byte Acc[2];

void setup()
{
  Serial.begin(115200);
  Serial1.begin(1000000, SERIAL_8N1, S_U0RXD, S_U0TXD); // 自定义串口
  st.pSerial = &Serial1;
  delay(1000);
  ID[0]=1;
  Speed[0]=500;
  Acc[0]=50;//Servo_1_Setting
  ID[1]=2;
  Speed[1]=2000;
  Acc[1]=50;//Servo_2_setting
  
}

void loop()
{
  int Pos;
  Position[0]=0;
  Position[1]=0;
  st.SyncWritePosEx(ID,2,Position,Speed,Acc);
  delay(5000);

 

  Position[0]=0;
  Position[1]=4095;
  st.SyncWritePosEx(ID,2,Position,Speed,Acc);
  delay(8500);

  

  Position[0]=0;
  Position[1]=0;
  st.SyncWritePosEx(ID,2,Position,Speed,Acc);
  delay(8500);

  
 
  


}
