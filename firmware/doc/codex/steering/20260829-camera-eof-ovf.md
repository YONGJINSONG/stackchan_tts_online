# 16 MHz XCLK for official PSRAM DMA

## Cause

`EV-EOF-OVF` is `ll_cam_send_event` failing to post `CAM_IN_SUC_EOF_EVENT`.
At 20 MHz XCLK, esp32-camera 2.0.4 memcpy's DRAM bounce buffers into PSRAM
and the queue overflows while Wi-Fi is up. Forcing `psram_mode` at 20 MHz
caused CoreS3 Guru Meditation.

## Fix

- Set XCLK to 16 MHz so `psram_mode = (xclk_freq_hz == 16000000)` is true.
  DMA goes straight to PSRAM. Do not force `psram_mode` in `cam_hal.c`.
- Keep GC0308 `0x28=0x32` (XCLK/4), 8-slot EOF queue, QVGA, 16 KB DMA cap.
- After deinit, `ledcDetachPin(2)` + `gpio_reset_pin(GPIO2)` so Port A servo
  Y can attach again.
- Retry `cam_take` on capture. Pause avatar from the 3-2-1 countdown.

## Verify

Serial: `stream restarted`, then `frame ready`. No reboot, no repeating
`EV-EOF-OVF`. After the session, `[camera] XCLK LEDC released on GPIO2`
and servo PWM attach on pins 1/2.
