#pragma once

#include <cstddef>
#include <cstdint>

namespace pokemon {

// Shared implementation behind updateSnapshotCrc32()/updateBattleStoreCrc32()/
// updateIvEvStoreCrc32() (PokemonStoreCodec.h/PokemonBattleStoreCodec.h/
// PokemonIvEvStoreCodec.h) - all three were byte-identical copies of the same
// bit-by-bit reflected CRC-32 (the standard 0xEDB88320 polynomial, IEEE
// 802.3/zlib/gzip's CRC-32), differing only in name. This is the same CRC,
// just computed 4 bits at a time via a 16-entry lookup table instead of 1 bit
// at a time - halves the per-byte work for a fixed 64 bytes of flash (the
// table), which is the right trade on a project where flash is the tighter
// budget of the two. Each store keeps its own thin wrapper function name
// (and its own CRC32_INITIAL/finish...Crc32 constants, which differ only in
// spelling, not value) so nothing outside those 3 files needs to change.
uint32_t updatePokemonCrc32(uint32_t crc, const uint8_t* data, size_t size);

}  // namespace pokemon
