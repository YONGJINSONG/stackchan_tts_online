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
// power / touch sensors. While a capture is in progress camera_is_busy() is true
// and those loop-tick sensors skip their I2C reads. Coexistence is UNVERIFIED.

void camera_vision_init();
bool camera_vision_look(const String& hint);   // capture + describe + speak/inject
String camera_vision_describe(const String& hint);  // capture + describe; empty on failure
bool camera_is_busy();

#endif  // _CAMERA_VISION_H
