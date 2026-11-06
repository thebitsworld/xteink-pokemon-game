// TLS memory in PSRAM on the X4 Pro (linked with --wrap, see [env:pokemon-x4-pro]): the prebuilt
// core's mbedTLS allocates from internal RAM only, and with the Game Boy core in IRAM the largest
// internal block (~32 KB) is too little for a handshake's record buffers. Small allocations stay
// internal, as they were.
#if CROSSINK_TLS_PSRAM

#include <esp_heap_caps.h>

#include <cstddef>

namespace {
constexpr size_t PSRAM_FROM_BYTES = 1024;
}

extern "C" {
void* __real_esp_mbedtls_mem_calloc(size_t n, size_t size);

void* __wrap_esp_mbedtls_mem_calloc(const size_t n, const size_t size) {
  if (n != 0 && size >= PSRAM_FROM_BYTES / n) {
    void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) return p;
  }
  return __real_esp_mbedtls_mem_calloc(n, size);
}
}

#endif  // CROSSINK_TLS_PSRAM
