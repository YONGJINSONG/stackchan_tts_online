#ifndef _GESTURE_H
#define _GESTURE_H

#include <Avatar.h>

extern volatile uint32_t gesture_suppress_until;
extern volatile bool g_gesture_playing;

void gesture_init();
void gesture_play(m5avatar::Expression e);
bool gesture_dance();

// Hold the head at a stable pose for camera/tutor operations.
void gesture_set_motion_hold(bool hold, bool center = false);
bool gesture_motion_held();

#endif  //_GESTURE_H
