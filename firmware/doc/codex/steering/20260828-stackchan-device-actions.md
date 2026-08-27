# Stack-chan device actions

## Realtime tools

- `dance` queues a fixed servo gesture and returns immediately.
- `set_display_power` controls only the LCD backlight. A first touch after
  display-off wakes the LCD and is not forwarded as a conversation tap.
- `take_photo(save)` is deferred to `loop()`. The WebSocket task keeps the call
  id and sends the real result only after capture, SD save and preview finish.

## CoreS3 camera ownership

The built-in camera shares GPIO 11/12 with the internal I2C bus and GPIO 2 with
the default Port A Y servo. Camera sessions and the main-loop sensor block use
the same mutex so a WebSocket-side vision request cannot release I2C while a
sensor transaction is in progress. Each capture therefore:

1. marks camera hardware busy and waits briefly for servo motion to finish;
2. detaches PWM servos, releases internal I2C and initializes the camera;
3. captures one frame using LEDC timer 3/channel 6;
4. deinitializes the camera, restores internal I2C and reattaches the servos.

All failure paths must perform the same restoration. Do not restore eager
camera initialization in `setup()`.

## Pet touch

A completed pet stroke consumes its release so it cannot also toggle Realtime
recording. Visual Happy/blush feedback is allowed while speech is playing;
optional sound and proactive speech remain suppressed until audio is idle.
