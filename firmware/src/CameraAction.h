#ifndef _CAMERA_ACTION_H
#define _CAMERA_ACTION_H

#include <Arduino.h>

// Realtime camera requests cross from the WebSocket task to loop(). Only one
// capture can be pending at a time. The WebSocket task later consumes the real
// success/failure result and sends the function_call_output.
bool camera_action_request_save();
void camera_action_process_pending();
bool camera_action_take_result(String& result);
void camera_action_abandon();

#endif
