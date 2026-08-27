#include "ServoCustom.h"
#include "ServoTrim.h"   // g_servoTrimX/Y — 홈 보정 오프셋(모든 이동에 더함)

void ServoCustom::moveToOrigin(){
  moveXY(_init_param.servo[AXIS_X].start_degree + g_servoTrimX,
         _init_param.servo[AXIS_Y].start_degree + g_servoTrimY, 1000);
}

void ServoCustom::moveTo(int degX, int degY){
  moveXY(_init_param.servo[AXIS_X].start_degree + g_servoTrimX + degX,
         _init_param.servo[AXIS_Y].start_degree + g_servoTrimY + degY, 1000);
}

void ServoCustom::moveTo(int degX, int degY, uint32_t millis_for_move){
  moveXY(_init_param.servo[AXIS_X].start_degree + g_servoTrimX + degX,
         _init_param.servo[AXIS_Y].start_degree + g_servoTrimY + degY, millis_for_move);
}

bool ServoCustom::suspendPwmForCamera(){
  if (_servo_type != ServoType::PWM) return false;
  _servo_x.detach();
  _servo_y.detach();
  Serial.println("[servo] PWM detached for CoreS3 camera GPIO2");
  return true;
}

void ServoCustom::resumePwmAfterCamera(){
  if (_servo_type != ServoType::PWM) return;
  attachServos();
  Serial.println("[servo] PWM restored after camera");
}
