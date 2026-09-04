#pragma once

#include <Arduino.h>

struct MbedTlsAllocatorStats {
    uint32_t psramAllocations = 0;
    size_t psramBytes = 0;
    uint32_t internalFallbackAllocations = 0;
    size_t internalFallbackBytes = 0;
};

// CoreS3 overrides ESP-IDF's mbedTLS allocator so its large TLS records do
// not compete with Realtime's internal-RAM audio and WebSocket resources.
void mbedTlsAllocatorResetStats();
MbedTlsAllocatorStats mbedTlsAllocatorStats();
