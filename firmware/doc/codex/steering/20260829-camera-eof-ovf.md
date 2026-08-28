# Restart GC0308 stream and DMA RGB565 into PSRAM

## Cause

`EV-EOF-OVF` is `ll_cam_send_event` failing to post `CAM_IN_SUC_EOF_EVENT`.
esp32-camera 2.0.4 enables S3 PSRAM DMA only when XCLK is 16 MHz. At the
required 20 MHz XCLK it memcpy's each 7,680-byte DRAM half-buffer into a
PSRAM frame. Realtime Wi-Fi holds that bus long enough for the 1-slot EOF
queue to overflow, even with `cam_task` on Core 1.

`esp_camera_init` also enables VSYNC at the GC0308 default `0x28=0x00`
(PCLK = 20 MHz) before we write `0x28=0x32`.

## Fix

- Apply `0x28=0x32` in the GC0308 reset path so the first `cam_start()` is
  already at ~5 MHz PCLK.
- After `esp_camera_init`, `cam_stop()`, confirm PCLK, `cam_start()`, drop
  two warmup frames.
- Force `psram_mode` on ESP32-S3 without lowering XCLK to 16 MHz, so RGB565
  DMA writes the PSRAM frame directly. Keep `CONFIG_CAMERA_DMA_BUFFER_SIZE_MAX`,
  QVGA, and 20 MHz XCLK.
- Give `cam_hal` at least 8 EOF queue slots and pause avatar drawing for the
  session.

## Verify

Serial after 「사진 찍어줘」 must include `eof-ovf fix: psram dma` and
`stream restarted`, then `frame ready`. DMA heap after init should stay close
to the before-init value (no 15 KB SRAM bounce). No repeating `EV-EOF-OVF`.
