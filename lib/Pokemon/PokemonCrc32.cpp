#if defined(CROSSINK_ENABLE_POKEMON)

#include "PokemonCrc32.h"

#include <array>

namespace pokemon {
namespace {

// table[i] is what running the standard reflected-CRC32 bit loop 4 times
// (one per bit of a nibble) produces starting from `i` in the low 4 bits -
// the well-known "half-byte" CRC-32 table technique. Applying it twice per
// input byte (once for the low nibble, once for the high nibble) is
// mathematically identical to running the bit-by-bit loop 8 times, just in
// 2 table lookups instead of 8 branchy shift/xor steps.
constexpr std::array<uint32_t, 16> makeNibbleTable() {
  constexpr uint32_t polynomial = 0xEDB88320U;
  std::array<uint32_t, 16> table{};
  for (uint32_t i = 0; i < table.size(); ++i) {
    uint32_t value = i;
    for (int bit = 0; bit < 4; ++bit) {
      value = (value & 1U) != 0U ? (value >> 1U) ^ polynomial : (value >> 1U);
    }
    table[i] = value;
  }
  return table;
}

constexpr std::array<uint32_t, 16> NIBBLE_TABLE = makeNibbleTable();

}  // namespace

uint32_t updatePokemonCrc32(uint32_t crc, const uint8_t* data, const size_t size) {
  for (size_t index = 0; index < size; ++index) {
    crc ^= data[index];
    crc = NIBBLE_TABLE[crc & 0xFU] ^ (crc >> 4U);
    crc = NIBBLE_TABLE[crc & 0xFU] ^ (crc >> 4U);
  }
  return crc;
}

}  // namespace pokemon

#endif
