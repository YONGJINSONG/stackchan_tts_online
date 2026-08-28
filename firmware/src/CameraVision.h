#ifndef _CAMERA_VISION_H
#define _CAMERA_VISION_H

#include <Arduino.h>

// GPT-4o-mini vision via a separate Chat Completions REST call.
// Cascade Function Calling uses camera_vision_describe() as the tool result.
// Realtime mode still injects the description with pushUserText via camera_vision_look().
//
// Only does anything in a build with -DENABLE_CAMERA. Always compiles (no-ops
// otherwise) so the web handler and sensor guards can call it unconditionally.
//
// Camera SCCB shares the CoreS3 internal I2C bus with the proximity / IMU /
// power / touch sensors. The loop-side sensor block and camera sessions use the
// same mutex, while camera_is_busy() lets non-I2C work skip stale sensor input.

void camera_vision_init();
bool camera_vision_look(const String& hint);   // capture + describe + speak/inject
String camera_vision_describe(const String& hint);  // capture + describe; empty on failure
bool camera_is_busy();
void camera_set_hardware_busy(bool busy);
void camera_sensor_bus_lock();
bool camera_sensor_bus_try_lock(uint32_t timeout_ms);
void camera_sensor_bus_unlock();

#endif  // _CAMERA_VISION_H
