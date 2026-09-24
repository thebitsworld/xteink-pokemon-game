#pragma once

#if defined(CROSSINK_ENABLE_POKEMON)

#include <PokemonMovesetStoreCodec.h>

#include <span>

namespace pokemon {

// Reads/writes /.crosspoint/pokemon-moves-{a,b}.bin: the permanent part of
// every customised Pokemon's battle data - its moveset and PP Up counts -
// for the Party and the PC Box alike (see PokemonMovesetStoreCodec.h for why
// this is split out of pokemon-battle-{a,b}.bin). Same double-buffered
// write-then-verify-then-flip pattern as the other side files: a write always
// lands on the currently-inactive file, verified by reading it back, before
// the active pointer flips. A missing or corrupt file just leaves the store
// empty - callers then derive the moveset from species and level, exactly
// as a never-customised Pokemon's is.
//
// Writes are rare (only when a moveset or PP Up count actually changes), so
// rewriting the whole file each time is fine; all scratch buffers are
// heap-allocated with non-throwing new for the same reasons documented in
// PokemonIvEvStore.cpp.
class PokemonMovesetStore {
 public:
  // Reads whichever of the two files is newest. Called automatically by every
  // other method on first use (idempotent).
  void load() const;
  const MovesetEntry* findEntry(uint32_t recordId) const;
  // Inserts/replaces an entry and rewrites the whole (inactive) file, then
  // flips the active pointer only after a read-back verifies it. False for a
  // validation failure, a full store, or a write failure - the in-memory
  // state and both on-disk files are left as they were in every such case.
  bool upsertEntry(const MovesetEntry& entry);
  // Applies every entry to one candidate state and writes it once. Entries
  // that fail validation or don't fit are skipped rather than aborting the
  // batch; false (nothing written) only if not one entry could be applied.
  bool upsertEntries(std::span<const MovesetEntry> entries);
  // Removes one entry (same commit as upsertEntry). False if absent or on a
  // write failure.
  bool removeEntry(uint32_t recordId);
  // Writes an empty state. Used by PokemonService::reset() so a fresh game
  // doesn't read back a previous playthrough's movesets once record ids start
  // over from 1.
  bool reset();

 private:
  bool writeState(const MovesetStoreState& state) const;

  mutable MovesetStoreState state_{};
  mutable bool loaded_ = false;
  mutable bool ready_ = false;      // at least one of the two files currently holds a valid, decoded state
  mutable bool activeIsA_ = false;  // which of the two files ready_ refers to
  mutable uint32_t sequence_ = 0;   // sequence number of the active file; 0 when !ready_
};

}  // namespace pokemon

#endif
