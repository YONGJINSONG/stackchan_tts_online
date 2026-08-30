#include <Arduino.h>
#include <esp_arduino_version.h>

#include "ServoCustom.h"
#include "ServoTrim.h"   // g_servoTrimX/Y — 홈 보정 오프셋

#include "driver/gpio.h"


namespace {

// ServoEasing/SG90의 실제 PWM 각도 범위
inline int clampServoDegree(int degree) {
  return constrain(degree, 0, 180);
}


// Arduino-ESP32 2.x / 3.x LEDC API 호환
inline void detachLedcCompat(int pin) {
  if (pin < 0) return;

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  // Arduino-ESP32 3.x
  (void)ledcDetach((uint8_t)pin);
#else
  // Arduino-ESP32 2.x
  ledcDetachPin((uint8_t)pin);
#endif
}


// GPIO reset 안전 처리
inline void resetGpioSafe(int pin) {
  if (pin < 0 || pin >= GPIO_NUM_MAX) return;
  gpio_reset_pin((gpio_num_t)pin);
  gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
}

}  // namespace


void ServoCustom::moveToOrigin() {
  if (_pwm_held_for_camera) return;
  moveXY(
      _init_param.servo[AXIS_X].start_degree + g_servoTrimX,
      _init_param.servo[AXIS_Y].start_degree + g_servoTrimY,
      1000);
}


void ServoCustom::moveTo(int degX, int degY) {
  if (_pwm_held_for_camera) return;
  moveXY(
      _init_param.servo[AXIS_X].start_degree + g_servoTrimX + degX,
      _init_param.servo[AXIS_Y].start_degree + g_servoTrimY + degY,
      1000);
}


void ServoCustom::moveTo(
    int degX,
    int degY,
    uint32_t millis_for_move) {
  if (_pwm_held_for_camera) return;
  moveXY(
      _init_param.servo[AXIS_X].start_degree + g_servoTrimX + degX,
      _init_param.servo[AXIS_Y].start_degree + g_servoTrimY + degY,
      millis_for_move);
}


// -----------------------------------------------------------------------------
// Camera / Servo PWM sharing
// -----------------------------------------------------------------------------

bool ServoCustom::suspendPwmForCamera() {
  if (_servo_type != ServoType::PWM) {
    return false;
  }

  _pwm_held_for_camera = true;

  // _last_degree_x / _last_degree_y는 그대로 유지한다.
  // 카메라 종료 후 같은 위치를 복원하기 위해 필요하다.
  _servo_x.detach();
  _servo_y.detach();

  Serial.printf(
      "[servo] PWM detached for camera. last=%d/%d\n",
      _last_degree_x,
      _last_degree_y);

  return true;
}


void ServoCustom::resumePwmAfterCamera() {
  _pwm_held_for_camera = false;
  dumpAndReattach("camera");
}


// -----------------------------------------------------------------------------
// Diagnostics / Pin switching
// -----------------------------------------------------------------------------

void ServoCustom::reattachOnPins(
    int pinX,
    int pinY,
    const char* reason) {

  if (_servo_type == ServoType::PWM) {
    _servo_x.detach();
    _servo_y.detach();
  }

  _init_param.servo[AXIS_X].pin = pinX;
  _init_param.servo[AXIS_Y].pin = pinY;

  dumpAndReattach(reason);
}


int ServoCustom::currentPinX() const {
  return _init_param.servo[AXIS_X].pin;
}


int ServoCustom::currentPinY() const {
  return _init_param.servo[AXIS_Y].pin;
}


ServoType ServoCustom::servoType() const {
  return _servo_type;
}


// -----------------------------------------------------------------------------
// Fast direct movement
// -----------------------------------------------------------------------------

void ServoCustom::writeOffset(int degX, int degY) {

  if (_pwm_held_for_camera) return;
  // PWM 이외의 서보 타입은 기존 moveXY 경로를 사용
  if (_servo_type != ServoType::PWM) {
    moveTo(degX, degY, 80);
    return;
  }

  // Stackchan이 기억하는 논리적 각도
  const int commandX =
      _init_param.servo[AXIS_X].start_degree +
      g_servoTrimX +
      degX;

  const int commandY =
      _init_param.servo[AXIS_Y].start_degree +
      g_servoTrimY +
      degY;

  // 실제 PWM 출력은 Stackchan 설정 offset까지 포함해야 한다.
  const int physicalX = clampServoDegree(
      commandX + _init_param.servo[AXIS_X].offset);

  const int physicalY = clampServoDegree(
      commandY + _init_param.servo[AXIS_Y].offset);

  _servo_x.write(physicalX);
  _servo_y.write(physicalY);

  // _last_degree는 offset 적용 전의 논리적 위치를 저장한다.
  // StackchanSERVO::moveXY()와 동일한 의미를 유지.
  _last_degree_x = commandX;
  _last_degree_y = commandY;
}


// -----------------------------------------------------------------------------
// Diagnostic hard sweep
// -----------------------------------------------------------------------------

void ServoCustom::hardSweep(const char* reason) {

  dumpAndReattach(reason);

  if (_servo_type != ServoType::PWM) {
    return;
  }

  Serial.printf(
      "[servo] hardSweep(%s) write 0/180/90\n",
      reason ? reason : "?");

  _servo_x.write(0);
  _servo_y.write(0);
  delay(700);

  _servo_x.write(180);
  _servo_y.write(180);
  delay(700);

  _servo_x.write(90);
  _servo_y.write(90);
  delay(500);

  // 진단 완료 후 논리적 위치도 90도로 맞춰둔다.
  _last_degree_x =
      90 - _init_param.servo[AXIS_X].offset;

  _last_degree_y =
      90 - _init_param.servo[AXIS_Y].offset;
}


// -----------------------------------------------------------------------------
// PWM reattach
// -----------------------------------------------------------------------------

void ServoCustom::dumpAndReattach(const char* reason) {

  const int pinX = _init_param.servo[AXIS_X].pin;
  const int pinY = _init_param.servo[AXIS_Y].pin;

  // 중요:
  // detach 전에 마지막 논리적 위치를 보존한다.
  //
  // _last_degree는 StackchanSERVO가 offset 적용 전 좌표로 관리한다.
  const int restoreCommandX = _last_degree_x;
  const int restoreCommandY = _last_degree_y;

  Serial.printf(
      "[servo] dump(%s)"
      " type=%d"
      " pinX=%d pinY=%d"
      " center=%d/%d"
      " last=%d/%d\n",
      reason ? reason : "?",
      (int)_servo_type,
      pinX,
      pinY,
      _init_param.servo[AXIS_X].start_degree,
      _init_param.servo[AXIS_Y].start_degree,
      restoreCommandX,
      restoreCommandY);

  if (_servo_type != ServoType::PWM) {
    Serial.println(
        "[servo] skip reattach: not PWM type");
    return;
  }


  // ---------------------------------------------------------------------------
  // 기존 LEDC / GPIO 상태 완전히 정리
  // ---------------------------------------------------------------------------

  detachLedcCompat(pinX);

  if (pinY != pinX) {
    detachLedcCompat(pinY);
  }

  resetGpioSafe(pinX);

  if (pinY != pinX) {
    resetGpioSafe(pinY);
  }


  if (pinX == 2 || pinY == 2) {
    Serial.println(
        "[servo] GPIO2 reset before PWM attach "
        "(CoreS3 camera XCLK)");
  }

  delay(5);


  // ---------------------------------------------------------------------------
  // 카메라 촬영 전의 위치로 PWM 복원
  // ---------------------------------------------------------------------------

  const int attachDegreeX = clampServoDegree(
      restoreCommandX +
      _init_param.servo[AXIS_X].offset);

  const int attachDegreeY = clampServoDegree(
      restoreCommandY +
      _init_param.servo[AXIS_Y].offset);


  uint8_t xResult = _servo_x.attach(
      pinX,
      attachDegreeX,
      DEFAULT_MICROSECONDS_FOR_0_DEGREE,
      DEFAULT_MICROSECONDS_FOR_180_DEGREE);


  uint8_t yResult = _servo_y.attach(
      pinY,
      attachDegreeY,
      DEFAULT_MICROSECONDS_FOR_0_DEGREE,
      DEFAULT_MICROSECONDS_FOR_180_DEGREE);


  _servo_x.setEasingType(EASE_QUADRATIC_IN_OUT);
  _servo_y.setEasingType(EASE_QUADRATIC_IN_OUT);


  // ServoEasing에서는 INVALID_SERVO만 실패이다.
  // 0도 정상적인 반환값이 될 수 있다.
  const bool xOk = (xResult != INVALID_SERVO);
  const bool yOk = (yResult != INVALID_SERVO);


  if (xOk && yOk) {

    // 촬영 전 논리 위치 유지
    _last_degree_x = restoreCommandX;
    _last_degree_y = restoreCommandY;

    Serial.printf(
        "[servo] PWM attach OK (%s)"
        " x=%u y=%u"
        " restored=%d/%d"
        " physical=%d/%d\n",
        reason ? reason : "?",
        (unsigned)xResult,
        (unsigned)yResult,
        restoreCommandX,
        restoreCommandY,
        attachDegreeX,
        attachDegreeY);

  } else {

    Serial.printf(
        "[servo] PWM attach FAIL (%s):"
        " x=%u y=%u\n",
        reason ? reason : "?",
        (unsigned)xResult,
        (unsigned)yResult);
  }
}
