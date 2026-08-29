#pragma once

#include <sdkconfig.h>

#if defined(CONFIG_IDF_TARGET_ESP32S3) && defined(ENABLE_CAMERA)

/*
 * CoreS3 + GC0308 RGB565:
 *
 * Direct PSRAM DMA is PSRAM-bus contention that can tear RGB565 frames
 * (horizontal bands). Use an internal-RAM DMA bounce buffer and let
 * the CPU copy into the PSRAM framebuffer.
 *
 * QVGA RGB565 is 153600 bytes. A 16 KB ceiling makes ~20 EOF copies per
 * frame, which overflows an 16-slot queue if memcpy stalls once. 32 KB
 * drops that to ~10 copies per frame.
 */

#undef CONFIG_CAMERA_DMA_BUFFER_SIZE_MAX
#define CONFIG_CAMERA_DMA_BUFFER_SIZE_MAX 32768

#undef CONFIG_CAMERA_PSRAM_DMA
#define CONFIG_CAMERA_PSRAM_DMA 0

/*
 * Camera copy task on Core 1 so Wi-Fi / WebSocket on Core 0 is not starved.
 */
#undef CONFIG_CAMERA_CORE0
#undef CONFIG_CAMERA_CORE1
#define CONFIG_CAMERA_CORE1 1

#undef CONFIG_CAMERA_TASK_STACK_SIZE
#define CONFIG_CAMERA_TASK_STACK_SIZE 4096
#endif
