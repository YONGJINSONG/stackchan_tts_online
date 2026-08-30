#ifndef _SERVO_CUSTOM_H
#define _SERVO_CUSTOM_H

#include <Stackchan_servo.h>

class ServoCustom : public StackchanSERVO {

public:
    ServoCustom() {};

    void moveToOrigin();

    void moveTo(int degX, int degY);

    void moveTo(
        int degX,
        int degY,
        uint32_t millis_for_move);

    // Camera가 GPIO2를 사용하는 동안 PWM servo 출력 일시 중지.
    // Camera XCLK uses LEDC; a later moveTo() must not reattach PWM
    // until resumePwmAfterCamera().
    bool suspendPwmForCamera();

    // Camera 종료 후 PWM 복구
    void resumePwmAfterCamera();

    // PWM 상태 출력 및 재부착
    void dumpAndReattach(const char* reason);

    // Servo GPIO 변경 + 재부착
    void reattachOnPins(
        int pinX,
        int pinY,
        const char* reason);

    // 현재 PWM GPIO 확인
    int currentPinX() const;
    int currentPinY() const;

    // Active backend selected from SC_BasicConfig.yaml.
    ServoType servoType() const;

    // Easing 없이 빠르게 상대 위치 이동
    void writeOffset(int degX, int degY);

    // 진단용 강제 sweep
    void hardSweep(const char* reason);

private:
    bool _pwm_held_for_camera = false;
};

#endif  // _SERVO_CUSTOM_H
