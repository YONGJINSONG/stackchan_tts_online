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

The built-in GC0308 has no JPEG output. Keep captures at QVGA RGB565 while
Realtime Wi-Fi is active, then convert the frame to JPEG after capture. VGA
RGB565 puts excessive pressure on the camera DMA/PSRAM path and can stall at
`esp_camera_fb_get()`. Camera acquisition must also use a bounded sensor-bus
wait so a leaked or contended I2C owner produces an error instead of freezing
the main loop. Before writing the fixed `.preview.jpg` path, remove any stale
file because Arduino SD `FILE_WRITE` appends to an existing file.

CoreS3 camera builds force `CONFIG_CAMERA_DMA_BUFFER_SIZE_MAX=16384`. With
QVGA RGB565 and 20 MHz XCLK, esp32-camera 2.0.4 then allocates an approximately
15,360-byte internal DMA buffer instead of 30,720 bytes. Keep this override
limited to `m5stack-cores3-camera` and `m5stack-cores3-realtime-camera`; do not
switch to the driver's experimental 16 MHz PSRAM-DMA path. Log total and
largest DMA-capable heap before init, after init, and after deinit so repeated
captures can reveal fragmentation or leaks.

Arduino-ESP32 ships `libesp32-camera.a` precompiled, so ordinary project build
flags cannot change its DMA limit. The camera-only PlatformIO pre-script builds
the ESP32-S3 `ll_cam.c` unit from the pinned esp32-camera 2.0.4 dependency and
links it into the main program with the forced configuration header. The final
map must resolve `ll_cam_config` from `camera_dma/ll_cam.o`, not from the
framework archive.

The same camera-only pre-script source-builds `cam_hal.c` with the override
header. Arduino-ESP32 normally pins its camera copy task to Core 0, where
Realtime Wi-Fi/WebSocket work can starve the one-slot EOF event queue. Pin the
camera task to Core 1 instead; the synchronous capture caller is waiting there,
so the high-priority copy task can drain each DMA half without changing the
rest of the Realtime task layout. The final map must resolve `cam_init` from
`camera_hal/cam_hal.o`.

The GC0308 driver applies a sensor-side PCLK divider on the original ESP32 to
keep PCLK at or below 15 MHz, but omits it on ESP32-S3. On CoreS3, set the full
GC0308 clock-divider register to `0x28=0x32`: bits 6:4 select XCLK / 4 and bits
2:0 select a balanced 2:2 duty cycle. Camera XCLK remains 20 MHz while the
roughly 5 MHz sensor output prevents `EV-EOF-OVF` with the smaller DMA
ping-pong buffer.

Deferred camera calls include a countdown and user review buttons. Disable the
Realtime response watchdog while that UI is pending and re-arm it only after
the function result has been sent.

## Pet touch

A completed pet stroke consumes its release so it cannot also toggle Realtime
recording. Visual Happy/blush feedback is allowed while speech is playing;
optional sound and proactive speech remain suppressed until audio is idle.
