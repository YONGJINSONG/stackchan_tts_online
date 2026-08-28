#ifndef _CAMERA_ACTION_H
#define _CAMERA_ACTION_H

#include <Arduino.h>

// Realtime camera requests cross from the WebSocket task to loop(). Capture is
// followed by on-screen Save / Don't save. The WebSocket task consumes the
// result after that tap finishes.
bool camera_action_request_save();
void camera_action_process_pending();
bool camera_action_take_result(String& result);
void camera_action_abandon();
bool camera_action_is_ui_active();
void camera_action_on_touch(int16_t x, int16_t y);

#endif
