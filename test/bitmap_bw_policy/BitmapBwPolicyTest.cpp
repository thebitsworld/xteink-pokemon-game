#include <BitmapHelpers.h>

#include <cstdint>
#include <iostream>

namespace {

bool check(const bool condition, const char* message) {
  if (condition) return true;
  std::cerr << message << '\n';
  return false;
}

int countWhite(const uint8_t gray) {
  int count = 0;
  for (int y = 0; y < 16; ++y) {
    for (int x = 0; x < 16; ++x) {
      count += quantize1bit(gray, x, y);
    }
  }
  return count;
}

}  // namespace

int main() {
  if (!check(countWhite(0) == 0, "Black produced white pixels")) return 1;
  if (!check(countWhite(255) == 256, "White produced black pixels")) return 1;

  const int darkWhite = countWhite(96);
  const int lightWhite = countWhite(160);
  if (!check(darkWhite > 0 && darkWhite < 256, "Dark gray did not dither")) return 1;
  if (!check(lightWhite > 0 && lightWhite < 256, "Light gray did not dither")) return 1;
  if (!check(darkWhite < lightWhite, "Dark gray was not darker than light gray")) return 1;

  return 0;
}
