#pragma once

#include <esp_heap_caps.h>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <type_traits>
#include <vector>

// Long-lived STL containers should not fragment the internal DMA-capable heap.
// PSRAM is preferred, while boards without usable PSRAM retain the old internal
// RAM behavior.
template <typename T>
class SpiRamStlAllocator {
public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using propagate_on_container_move_assignment = std::true_type;
  using is_always_equal = std::true_type;

  template <typename U>
  struct rebind { using other = SpiRamStlAllocator<U>; };

  SpiRamStlAllocator() noexcept = default;

  template <typename U>
  SpiRamStlAllocator(const SpiRamStlAllocator<U>&) noexcept {}

  T* allocate(size_type count) {
    if (count == 0) return nullptr;
    if (count > max_size()) std::abort();
    const size_type bytes = count * sizeof(T);
    void* ptr = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) ptr = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
    if (!ptr) std::abort();
    return static_cast<T*>(ptr);
  }

  void deallocate(T* ptr, size_type) noexcept {
    heap_caps_free(ptr);
  }

  constexpr size_type max_size() const noexcept {
    return std::numeric_limits<size_type>::max() / sizeof(T);
  }
};

template <typename T, typename U>
constexpr bool operator==(const SpiRamStlAllocator<T>&,
                          const SpiRamStlAllocator<U>&) noexcept {
  return true;
}

template <typename T, typename U>
constexpr bool operator!=(const SpiRamStlAllocator<T>&,
                          const SpiRamStlAllocator<U>&) noexcept {
  return false;
}

template <typename T>
using SpiRamVector = std::vector<T, SpiRamStlAllocator<T>>;
