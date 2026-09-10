#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#if defined(ARDUINO_ARCH_ESP32) && !defined(SIMULATOR)
#include <esp_heap_caps.h>
#endif

// Nothrow versions of std::make_unique. Return nullptr on allocation failure
// instead of calling abort() (the default when exceptions are disabled on ESP32).
//
// Single object:
//   auto obj = makeUniqueNoThrow<PNG>();
//   if (!obj) { LOG_ERR("TAG", "OOM"); return false; }
//
// Array:
//   auto buf = makeUniqueNoThrow<uint8_t[]>(size);
//   if (!buf) { LOG_ERR("TAG", "OOM"); return false; }
//   buf[0] = 0xFF;
//   someApi(buf.get(), size);
//

template <typename T, typename... Args>
  requires(!std::is_array_v<T>)
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  return std::unique_ptr<T>(new (std::nothrow) T(std::forward<Args>(args)...));
}

template <typename T>
  requires std::is_unbounded_array_v<T>
std::unique_ptr<T> makeUniqueNoThrow(size_t count) {
  using Elem = std::remove_extent_t<T>;
  return std::unique_ptr<T>(new (std::nothrow) Elem[count]());
}

// malloc-backed byte buffers for capability-specific ESP32 heaps. These are
// runtime-sized working buffers, so stack/static storage is not suitable.
// The custom deleter keeps ownership automatic on every early-return path.
struct HeapByteBufferDeleter {
  void operator()(uint8_t* ptr) const {
#if defined(ARDUINO_ARCH_ESP32) && !defined(SIMULATOR)
    heap_caps_free(ptr);
#else
    std::free(ptr);
#endif
  }
};

using HeapByteBuffer = std::unique_ptr<uint8_t[], HeapByteBufferDeleter>;

inline bool psramHeapAvailable() {
#if defined(ARDUINO_ARCH_ESP32) && !defined(SIMULATOR)
  return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
#else
  return false;
#endif
}

inline HeapByteBuffer makeHeapByteBufferNoThrow(const size_t count) {
  if (count == 0) return {};
#if defined(ARDUINO_ARCH_ESP32) && !defined(SIMULATOR)
  return HeapByteBuffer(static_cast<uint8_t*>(heap_caps_malloc(count, MALLOC_CAP_DEFAULT)));
#else
  return HeapByteBuffer(static_cast<uint8_t*>(std::malloc(count)));
#endif
}

inline HeapByteBuffer makePsramByteBufferNoThrow(const size_t count) {
  if (count == 0) return {};
#if defined(ARDUINO_ARCH_ESP32) && !defined(SIMULATOR)
  return HeapByteBuffer(static_cast<uint8_t*>(heap_caps_malloc(count, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
#else
  (void)count;
  return {};
#endif
}

// Helper struct to call a cleanup function on exit from any scope.
// Use with a lambda to avoid unnecessary allocations from std::function/std::bind:
// Example:
//   auto jpeg = makeUniqueNoThrow<JPEGDEC>();
//   ScopedCleanup cleanup{[&jpeg]{ jpeg->close(); }};
//
template <typename F>
struct [[nodiscard]] ScopedCleanup final {
  const F fn;
  explicit ScopedCleanup(F f) : fn{std::move(f)} {}
  ScopedCleanup(const ScopedCleanup&) = delete;
  ScopedCleanup& operator=(const ScopedCleanup&) = delete;
  ScopedCleanup(ScopedCleanup&&) = delete;
  ScopedCleanup& operator=(ScopedCleanup&&) = delete;
  ~ScopedCleanup() { fn(); }
};

template <typename F>
ScopedCleanup(F) -> ScopedCleanup<F>;
