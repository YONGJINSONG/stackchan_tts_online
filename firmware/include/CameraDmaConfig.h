#pragma once

#include <sdkconfig.h>

#if defined(CONFIG_IDF_TARGET_ESP32S3) && defined(ENABLE_CAMERA)

/*
 * CoreS3 + GC0308 RGB565:
 *
 * Direct PSRAM DMA tears frames (horizontal bands). Use an internal-RAM
 * DMA bounce buffer and CPU-copy into the PSRAM framebuffer.
 *
 * The framework camera archive is compiled with a 32768-byte ceiling and uses
 * a 30720-byte bounce buffer for QVGA RGB565. Physical tests showed that both
 * 7680-byte and 15360-byte source-built LL buffers overflow before a frame can
 * complete, so keep this diagnostic value aligned with the stock archive.
 */

#undef CONFIG_CAMERA_DMA_BUFFER_SIZE_MAX
#define CONFIG_CAMERA_DMA_BUFFER_SIZE_MAX 32768

#undef CONFIG_CAMERA_PSRAM_DMA
#define CONFIG_CAMERA_PSRAM_DMA 0

#undef CONFIG_CAMERA_CORE0
#undef CONFIG_CAMERA_CORE1
#define CONFIG_CAMERA_CORE1 1

#undef CONFIG_CAMERA_TASK_STACK_SIZE
#define CONFIG_CAMERA_TASK_STACK_SIZE 4096
#endif
