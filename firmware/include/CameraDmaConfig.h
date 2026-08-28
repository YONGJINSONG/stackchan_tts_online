#pragma once

#include <sdkconfig.h>

// esp32-camera 2.0.4 defaults to a 32 KiB internal DMA ping-pong buffer on
// ESP32-S3. Realtime TLS/audio fragments internal RAM enough that the resulting
// 30,720-byte QVGA allocation can fail. Keep 20 MHz XCLK and reduce only the
// camera-build DMA ceiling; ll_cam then requests a 15,360-byte buffer.
#if defined(CONFIG_IDF_TARGET_ESP32S3) && defined(ENABLE_CAMERA)
#undef CONFIG_CAMERA_DMA_BUFFER_SIZE_MAX
#define CONFIG_CAMERA_DMA_BUFFER_SIZE_MAX 16384

// Arduino-ESP32 pins esp32-camera's copy task to Core 0 by default. Realtime
// Wi-Fi/WebSocket work also runs there and can starve the one-slot DMA event
// queue. During a synchronous capture the application loop on Core 1 is
// waiting, so run the high-priority copy task there instead.
#undef CONFIG_CAMERA_CORE0
#undef CONFIG_CAMERA_CORE1
#define CONFIG_CAMERA_CORE1 1

#undef CONFIG_CAMERA_TASK_STACK_SIZE
#define CONFIG_CAMERA_TASK_STACK_SIZE 4096
#endif
