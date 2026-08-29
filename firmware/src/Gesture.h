#ifndef _GESTURE_H
#define _GESTURE_H

#include <Avatar.h>

extern volatile uint32_t gesture_suppress_until;
extern volatile bool g_gesture_playing;

void gesture_init();
void gesture_play(m5avatar::Expression e);
bool gesture_dance();

#endif  //_GESTURE_H
