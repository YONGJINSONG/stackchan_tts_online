#pragma once

#include <sdkconfig.h>

#if defined(CONFIG_IDF_TARGET_ESP32S3) && defined(ENABLE_CAMERA)

/*
 * CoreS3 + GC0308 RGB565:
 *
 * Direct PSRAM DMA tears frames (horizontal bands). Use an internal-RAM
 * DMA bounce buffer and CPU-copy into the PSRAM framebuffer.
 *
 * DMA 8192 keeps the bounce buffer small next to Wi-Fi. Camera task Core 1.
 */

#undef CONFIG_CAMERA_DMA_BUFFER_SIZE_MAX
#define CONFIG_CAMERA_DMA_BUFFER_SIZE_MAX 8192

#undef CONFIG_CAMERA_PSRAM_DMA
#define CONFIG_CAMERA_PSRAM_DMA 0

#undef CONFIG_CAMERA_CORE0
#undef CONFIG_CAMERA_CORE1
#define CONFIG_CAMERA_CORE1 1

#undef CONFIG_CAMERA_TASK_STACK_SIZE
#define CONFIG_CAMERA_TASK_STACK_SIZE 4096
#endif
