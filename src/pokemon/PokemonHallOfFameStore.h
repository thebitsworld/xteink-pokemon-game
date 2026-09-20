#pragma once

#if defined(CROSSINK_ENABLE_POKEMON)

#include <PokemonHallOfFameCodec.h>

namespace pokemon {

// Reads/writes /.crosspoint/pokemon-hof-{a,b}.bin: the one-time Hall of Fame
// snapshot taken the moment the Champion is first defeated. Same
// double-buffered write-then-verify-then-flip pattern as
// pokemon-battle-{a,b}.bin/pokemon-ivev-{a,b}.bin. Unlike those two, this
// store never holds a collection keyed by recordId - there is exactly one
// snapshot, ever, for the whole save.
class PokemonHallOfFameStore {
 public:
  // Reads whichever of the two files is newest. Called automatically by
  // every other method on first use (idempotent, so an explicit call is only
  // needed to force a re-read).
  void load() const;
  // True once a Hall of Fame snapshot has ever been captured (a valid file
  // exists) - a fresh install, or a save from before this feature shipped,
  // has neither file and this returns false.
  bool hasEntry() const;
  // nullptr when hasEntry() is false.
  const HallOfFameState* entry() const;
  // Writes the snapshot - fails (returns false, nothing written) if an entry
  // already exists, since the Champion can never be re-fought and this
  // snapshot must never be overwritten once captured.
  // PokemonService::captureHallOfFame() is this store's only writer and
  // already guards this the same way; the store itself refuses too,
  // belt-and-suspenders.
  bool captureOnce(const HallOfFameState& state);
  // Writes an empty (not-yet-captured) state (same double-buffer commit as
  // captureOnce). Used by PokemonService::reset() so a fresh game doesn't
  // read back a previous playthrough's Hall of Fame.
  bool reset();

 private:
  bool writeState(const HallOfFameState& state) const;

  mutable HallOfFameState state_{};
  mutable bool loaded_ = false;
  mutable bool ready_ = false;      // at least one of the two files currently holds a valid, decoded state
  mutable bool activeIsA_ = false;  // which of the two files ready_ refers to
  mutable uint32_t sequence_ = 0;   // sequence number of the active file; 0 when !ready_
};

PokemonHallOfFameStore& devicePokemonHallOfFameStore();

}  // namespace pokemon

#endif
