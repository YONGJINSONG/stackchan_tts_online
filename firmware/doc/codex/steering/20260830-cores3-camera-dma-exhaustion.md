# CoreS3 camera DMA exhaustion recovery

## Purpose

Restore `take_photo(output=save)` after Kids Tutor and Realtime use without a reboot. The original failure was internal DMA exhaustion: the framework S3 low-level driver requires a 30720-byte contiguous DMA buffer. Moving Kids Tutor's long-lived containers to PSRAM restored a 51188-byte largest DMA block. Physical testing then showed `EV-EOF-OVF` with both 7680-byte and 15360-byte source-built LL buffers, so the known-good framework LL is retained.

## Scope

- Keep the framework's precompiled ESP32-S3 `cam_hal` and `ll_cam` pair, which uses a 30720-byte QVGA RGB565 bounce buffer.
- Keep the stock `cam_hal.c`, camera clock, EOF queue, PCLK, lifecycle, GC0308 write-only sensor patch, preview conversion, and public photo API unchanged.
- Move long-lived Kids Tutor container backing storage to PSRAM with an internal-RAM fallback.
- Add DMA heap diagnostics and reject an obviously under-sized heap before resetting or probing the camera.

## Implementation notes

- Camera initialization requires a 30720-byte largest DMA block. A memory failure is not retried as a sensor failure; all audio, WebSocket, servo, and bus ownership is still restored.
- PSRAM vectors remain private implementation details. Existing `std::vector` interfaces used by questions and UI do not change.
- Preserve all current uncommitted Gesture, IdleMotion, Realtime, Kids Tutor, and audio-asset work.

## Validation

- Build `m5stack-cores3-realtime-camera`, `m5stack-cores3-realtime`, and `m5stack-core2-realtime`.
- Confirm the final map selects `ll_cam` from the framework camera archive and does not link a project-built `camera_dma/ll_cam.o`.
- On CoreS3, test a fresh photo and then repeat Kids Tutor -> photo -> Realtime, checking for 320x240/153600-byte frames, correct BMP/preview colors, stable DMA/PSRAM, and no camera or I2S DMA allocation failures.
- Physical validation produced repeated `EV-EOF-OVF` at both 8192 and 16384 ceilings. The working framework 32768-byte ceiling is restored; the queue and clocks remain unchanged.
