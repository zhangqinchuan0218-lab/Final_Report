#include <SCServo.h>
SMS_STS st;
int ID_ChangeFrom = 1;   // 要更改ID的舵机原本ID，出厂默认为1
int ID_Changeto      = 5;   // 新的ID
void setup(){
  Serial1.begin(1000000, SERIAL_8N1, 44, 43);
  st.pSerial = &Serial1;
 

  st.unLockEprom(ID_ChangeFrom); //解锁EPROM-SAFE
  st.writeByte(ID_ChangeFrom, SMS_STS_ID, ID_Changeto);//更改ID
  st.LockEprom(ID_Changeto); // EPROM-SAFE上锁
}

void loop(){
}