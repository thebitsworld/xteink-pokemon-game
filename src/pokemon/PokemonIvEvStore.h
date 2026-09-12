#pragma once

#if defined(CROSSINK_ENABLE_POKEMON)

#include <PokemonIvEvStoreCodec.h>

namespace pokemon {

// Reads/writes /.crosspoint/pokemon-ivev-{a,b}.bin: every record's permanent
// IV/EV pair (party and PC box both - see PokemonIvEvStoreCodec.h). Same
// double-buffered write-then-verify-then-flip pattern as
// pokemon-battle-{a,b}.bin and the main pokemon-{a,b}.bin save: a write
// always lands on the currently-inactive file, verified by reading it back,
// before the active pointer flips. A missing or corrupt file (fresh
// install, or if both slots are ever lost) just leaves the store empty -
// callers treat "no entry for this recordId" as IV 0 / EV 0, exactly
// today's pre-this-feature stats, not an error.
class PokemonIvEvStore {
 public:
  // Reads whichever of the two files is newest. Called automatically by
  // every other method on first use (idempotent, so an explicit call is
  // only needed to force a re-read).
  void load() const;
  const IvEvEntry* findEntry(uint32_t recordId) const;
  // Inserts/replaces an entry and rewrites the whole (inactive) file, then
  // flips the active pointer only after a read-back verifies it. Returns
  // false only for a validation failure (see upsertIvEvEntry) or a write
  // failure; the in-memory state and both on-disk files are left as they
  // were before the call in either case.
  bool upsertEntry(const IvEvEntry& entry);
  // Writes an empty state (same double-buffer commit as upsertEntry). Used
  // by PokemonService::reset() so a fresh game doesn't read back a previous
  // playthrough's leftover IV/EV data once record ids start over from 1.
  bool reset();

 private:
  bool writeState(const IvEvStoreState& state) const;

  mutable IvEvStoreState state_{};
  mutable bool loaded_ = false;
  mutable bool ready_ = false;      // at least one of the two files currently holds a valid, decoded state
  mutable bool activeIsA_ = false;  // which of the two files ready_ refers to
  mutable uint32_t sequence_ = 0;   // sequence number of the active file; 0 when !ready_
};

PokemonIvEvStore& devicePokemonIvEvStore();

}  // namespace pokemon

#endif
