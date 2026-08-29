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
  dumpAndReattach("camera");
}

void ServoCustom::reattachOnPins(int pinX, int pinY, const char* reason){
  if (_servo_type == ServoType::PWM) {
    _servo_x.detach();
    _servo_y.detach();
  }
  _init_param.servo[AXIS_X].pin = pinX;
  _init_param.servo[AXIS_Y].pin = pinY;
  dumpAndReattach(reason);
}

void ServoCustom::hardSweep(const char* reason){
  dumpAndReattach(reason);
  if (_servo_type != ServoType::PWM) return;
  Serial.printf("[servo] hardSweep(%s) write 0/180/90\n", reason ? reason : "?");
  _servo_x.write(0);
  _servo_y.write(0);
  delay(700);
  _servo_x.write(180);
  _servo_y.write(180);
  delay(700);
  _servo_x.write(90);
  _servo_y.write(90);
  delay(500);
}

void ServoCustom::dumpAndReattach(const char* reason){
  const int pinX = _init_param.servo[AXIS_X].pin;
  const int pinY = _init_param.servo[AXIS_Y].pin;
  Serial.printf("[servo] dump(%s) type=%d pinX=%d pinY=%d center=%d/%d\n",
                reason ? reason : "?",
                (int)_servo_type,
                pinX,
                pinY,
                _init_param.servo[AXIS_X].start_degree,
                _init_param.servo[AXIS_Y].start_degree);
  if (_servo_type != ServoType::PWM) {
    Serial.println("[servo] skip reattach: not PWM type");
    return;
  }
  // StackchanSERVO::attachServos() treats ServoEasing's non-zero success
  // return as an error. Reattach the PWM-only branch here with the correct
  // INVALID_SERVO/zero checks so the serial log reflects the real result.
  uint8_t xResult = _servo_x.attach(
      pinX,
      _init_param.servo[AXIS_X].start_degree + _init_param.servo[AXIS_X].offset,
      DEFAULT_MICROSECONDS_FOR_0_DEGREE,
      DEFAULT_MICROSECONDS_FOR_180_DEGREE);
  uint8_t yResult = _servo_y.attach(
      pinY,
      _init_param.servo[AXIS_Y].start_degree + _init_param.servo[AXIS_Y].offset,
      DEFAULT_MICROSECONDS_FOR_0_DEGREE,
      DEFAULT_MICROSECONDS_FOR_180_DEGREE);
  _servo_x.setEasingType(EASE_QUADRATIC_IN_OUT);
  _servo_y.setEasingType(EASE_QUADRATIC_IN_OUT);
  _last_degree_x = _init_param.servo[AXIS_X].start_degree;
  _last_degree_y = _init_param.servo[AXIS_Y].start_degree;

  bool xOk = xResult != 0 && xResult != INVALID_SERVO;
  bool yOk = yResult != 0 && yResult != INVALID_SERVO;
  if (xOk && yOk) {
    Serial.printf("[servo] PWM attach OK (%s) x=%u y=%u\n",
                  reason ? reason : "?",
                  (unsigned)xResult, (unsigned)yResult);
  } else {
    Serial.printf("[servo] PWM attach FAIL (%s): x=%u y=%u\n",
                  reason ? reason : "?",
                  (unsigned)xResult, (unsigned)yResult);
  }
}
