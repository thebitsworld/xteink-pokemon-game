#include <Arduino.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>

#include "lib/Epub/Epub/converters/ImageDimsProbe.h"
#include "lib/Epub/Epub/converters/ImageToFramebufferDecoder.h"

namespace {

bool check(const bool condition, const char* message) {
  if (condition) return true;
  std::cerr << message << '\n';
  return false;
}

std::array<uint8_t, 24> pngHeader(const uint32_t width, const uint32_t height) {
  return {0x89,
          0x50,
          0x4E,
          0x47,
          0x0D,
          0x0A,
          0x1A,
          0x0A,
          0x00,
          0x00,
          0x00,
          0x0D,
          'I',
          'H',
          'D',
          'R',
          static_cast<uint8_t>(width >> 24),
          static_cast<uint8_t>(width >> 16),
          static_cast<uint8_t>(width >> 8),
          static_cast<uint8_t>(width),
          static_cast<uint8_t>(height >> 24),
          static_cast<uint8_t>(height >> 16),
          static_cast<uint8_t>(height >> 8),
          static_cast<uint8_t>(height)};
}

}  // namespace

int main() {
  ImageDimensions dimensions{-1, -1};

  // The probe only reports header dimensions for caching/estimation - it no
  // longer enforces the ~8 MP decode-time pixel budget itself (that limit
  // now lives in ImageToFramebufferDecoder::validateImageDimensions, applied
  // by the concrete JPEG/PNG decoders before they allocate a framebuffer).
  ImageDimsProbe validProbe;
  const auto validHeader = pngHeader(2048, 4096);
  validProbe.write(validHeader.data(), validHeader.size());
  if (!check(validProbe.getDimensions(dimensions), "The streaming probe rejected a valid 8 MP PNG header")) return 1;

  uint32_t lastYieldMs = 1000;
  imageDecoderTestMillis = 1249;
  imageDecoderTestDelayCalls = 0;
  ImageToFramebufferDecoder::yieldDuringDecode(lastYieldMs);
  if (!check(lastYieldMs == 1000 && imageDecoderTestDelayCalls == 0, "Decode yielded before 250 ms")) return 1;

  imageDecoderTestMillis = 1250;
  ImageToFramebufferDecoder::yieldDuringDecode(lastYieldMs);
  if (!check(lastYieldMs == 1250 && imageDecoderTestDelayCalls == 1, "Decode did not yield at 250 ms")) return 1;

  lastYieldMs = std::numeric_limits<uint32_t>::max() - 99U;
  imageDecoderTestMillis = 150;
  ImageToFramebufferDecoder::yieldDuringDecode(lastYieldMs);
  if (!check(lastYieldMs == 150 && imageDecoderTestDelayCalls == 2, "Decode yield failed across timer rollover"))
    return 1;

  return 0;
}
