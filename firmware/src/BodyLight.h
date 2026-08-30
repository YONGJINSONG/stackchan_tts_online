#ifndef _BODY_LIGHT_H
#define _BODY_LIGHT_H

#include <Arduino.h>

// Official CoreS3 Stack-chan body: 12 WS2812 LEDs driven by the PY32 expander
// (I2C 0x6F), not an ESP32 GPIO. Apply I2C writes from loop() via tick().

void   body_light_init();
void   body_light_tick();
bool   body_light_available();
String body_light_request(bool hasOn, bool on, const char* color);

#endif
