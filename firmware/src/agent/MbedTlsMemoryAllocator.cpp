#include "MbedTlsMemoryAllocator.h"

#if defined(ARDUINO_M5STACK_CORES3)

#include <esp_heap_caps.h>
#include <soc/soc_memory_types.h>

#include <limits.h>

namespace {

constexpr size_t PSRAM_PREFERRED_ALLOCATION_BYTES = 4 * 1024;

portMUX_TYPE allocatorStatsMux = portMUX_INITIALIZER_UNLOCKED;
MbedTlsAllocatorStats allocatorStats;

void recordLargeAllocation(void* ptr, size_t bytes) {
    if (ptr == nullptr) return;

    portENTER_CRITICAL(&allocatorStatsMux);
    if (esp_ptr_external_ram(ptr)) {
        ++allocatorStats.psramAllocations;
        allocatorStats.psramBytes += bytes;
    } else {
        ++allocatorStats.internalFallbackAllocations;
        allocatorStats.internalFallbackBytes += bytes;
    }
    portEXIT_CRITICAL(&allocatorStatsMux);
}

}  // namespace

// ESP-IDF normally routes all mbedTLS calloc calls to internal RAM. Supplying
// these application definitions makes the linker use this hybrid allocator
// instead, without changing the PlatformIO framework package.
extern "C" void* esp_mbedtls_mem_calloc(size_t n, size_t size) {
    if (size != 0 && n > SIZE_MAX / size) return nullptr;

    const size_t bytes = n * size;
    if (bytes < PSRAM_PREFERRED_ALLOCATION_BYTES) {
        return heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }

    void* ptr = heap_caps_calloc_prefer(
        n,
        size,
        2,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    recordLargeAllocation(ptr, bytes);
    return ptr;
}

extern "C" void esp_mbedtls_mem_free(void* ptr) {
    heap_caps_free(ptr);
}

void mbedTlsAllocatorResetStats() {
    portENTER_CRITICAL(&allocatorStatsMux);
    allocatorStats = MbedTlsAllocatorStats{};
    portEXIT_CRITICAL(&allocatorStatsMux);
}

MbedTlsAllocatorStats mbedTlsAllocatorStats() {
    portENTER_CRITICAL(&allocatorStatsMux);
    const MbedTlsAllocatorStats snapshot = allocatorStats;
    portEXIT_CRITICAL(&allocatorStatsMux);
    return snapshot;
}

#else

void mbedTlsAllocatorResetStats() {}

MbedTlsAllocatorStats mbedTlsAllocatorStats() {
    return MbedTlsAllocatorStats{};
}

#endif
