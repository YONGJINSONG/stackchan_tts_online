#ifndef _SERVO_CUSTOM_H
#define _SERVO_CUSTOM_H

#include <Stackchan_servo.h>

class ServoCustom : public StackchanSERVO {
public:
    ServoCustom(){};
    void moveToOrigin();
    void moveTo(int degX, int degY);
    void moveTo(int degX, int degY, uint32_t millis_for_move);
    bool suspendPwmForCamera();
    void resumePwmAfterCamera();
    // Reattach PWM and print pin/type/attach result (diagnostics).
    void dumpAndReattach(const char* reason);
    // Detach, switch GPIO pins, reattach (Port A vs Port C probe).
    void reattachOnPins(int pinX, int pinY, const char* reason);
    // Bypass easing: write 0→180→90 on both axes (max visible twitch).
    void hardSweep(const char* reason);

};

#endif  //_SERVO_CUSTOM_H
