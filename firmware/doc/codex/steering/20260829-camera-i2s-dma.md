# Free I2S DMA before CoreS3 camera init

## Cause

QVGA init asks ll_cam for a 15,360-byte internal DMA buffer. After a Realtime turn the largest DMA-capable block was ~4.5 KB because Mic/Speaker I2S still owned DMA RAM. Deferred `take_photo` skipped the normal `Speaker.end()` / `exitMutexAudio()` path, so that memory stayed allocated through countdown.

## Fix

- On the WebSocket task, before waiting for the deferred camera result: stop record, drain speaker, `Mic.end` / `Speaker.end`, release `mutexAudio` if this turn held it.
- `camera_session_begin` takes `mutexAudio`, ends I2S again, then `esp_camera_init`. `camera_session_end` always gives the mutex. Do not `Mic.begin` here.

Do not change `CONFIG_CAMERA_DMA_BUFFER_SIZE_MAX`, PCLK, or QVGA.

## Verify

Serial after 「사진 찍어줘」: deferred audio released with `largest` >= 15360, then `initialized QVGA RGB565`.
