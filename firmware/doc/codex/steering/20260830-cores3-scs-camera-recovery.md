# CoreS3 official Stack-chan servo and camera recovery

## Context

The official CoreS3 Stack-chan body uses SCS0009 servos behind the board PY32 controller, not direct GPIO PWM. The GC0308 camera also shares CoreS3 board resources, including the existing internal I2C bus and limited internal DMA memory.

## Decisions

- Use the `M5_SCS` profile with Serial2 RX/TX pins 7/6, centers 150/90, X range 0-300, and Y range 0-90.
- Expose the active `ServoType` from `ServoCustom` so Robot, ESP-NOW, and camera-related diagnostics use the same type decision.
- Apply CoreS3 pin remapping, PWM attachment probes, and hard sweeps only to PWM servos. For `M5_SCS`, use the PY32 power path and servo IDs 1/2.
- Keep the board's `M5.In_I2C` instance installed while the camera borrows its port for SCCB. Do not release or reinstall the bus at camera-session boundaries.
- Keep `pin_xclk = -1`; do not detach or reset GPIO2 when the camera uses the board-provided clock.
- Use stock ESP32-S3 camera clock selection and the public frame-buffer API. Warm up with two frames and allow one full camera-driver reinitialization after a missed capture.
- Keep the framework's precompiled ESP32-S3 `cam_hal` and `ll_cam` together. Custom EOF queue, task affinity, clock, lifecycle, and reduced-LL-buffer builds produced repeated EOF overflow and no completed frame, so those patches remain removed. The GC0308 sensor override still leaves PCLK at the framework default.
- Hold the servo at its configured origin for the entire photo countdown/capture. Kids Tutor holds the same origin while questions are active, then releases the hold and queues the dance only after `stagewin.wav` finishes.
- Retry release of the AW9523 camera-reset output and skip sensor probing unless reset is confirmed high.
- Avoid GC0308 read-modify-write operations in the CoreS3 camera override. A timed-out SCCB read can return the stale sensor-ID byte (`0x9B`) and corrupt RGB565 or QVGA setup. Use checked write-only values (`0x24=0xA6`, page-1 `0x53=0x80`/`0x55=0x01`) and keep register `0x14` in a software shadow initialized to `0x11`. Do not copy page-0 register `0x55` bit 7 into page 1; the resulting `0x81` write stalls SCCB on the CoreS3 GC0308.
- Stop microphone or speaker I2S only when it is actually running. While a deferred function or any response is in flight, keep proactive messages queued so a battery/idle/touch reaction cannot create a second response and reclaim I2S during the shutter or camera DMA handoff.
- Preserve the existing `take_photo` schema, result JSON, and save/cancel UI behavior.

## Validation

The following PlatformIO environments build successfully:

- `m5stack-cores3-realtime-camera`
- `m5stack-cores3-realtime`
- `m5stack-core2-realtime`

Hardware validation remains required: install the updated YAML as `/yaml/SC_BasicConfig.yaml`, reboot, confirm the PY32 and SCS IDs 1/2, exercise ESP-NOW movement, and repeat `take_photo(output=save)` ten times while checking DMA memory and error logs.
