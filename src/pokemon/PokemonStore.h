#pragma once

#include <PokemonGame.h>
#include <PokemonStoreCodec.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace pokemon {

enum class StoreBeginResult : uint8_t {
  Empty,
  Ready,
  Corrupt,
  Unsupported,
};

enum class PcOrder : uint8_t {
  CatchDate,
  PokedexNumber,
  Alphabetical,
};

class PokemonStore {
 public:
  StoreBeginResult begin();
  bool isReady() const { return ready_; }
  uint32_t recordCount() const { return ready_ ? activeHeader_.recordCount : 0; }
  // The id a newly-created record should use. NOT recordCount() + 1 - once a
  // record can be removed (RecordMutationKind::Remove), recordCount() drops
  // but existing ids do not shift down, so ids are no longer dense and
  // recordCount() + 1 can collide with a still-live record. highestRecordId_
  // is tracked precisely because of this - see inspectSnapshot()'s own doc
  // comment in the .cpp.
  uint32_t nextRecordId() const { return highestRecordId_ + 1U; }
  bool loadState(PokemonState& output) const;
  bool readRecord(uint32_t recordId, PokemonRecord& output) const;
  bool loadOwnedEvolutionNeeds(OwnedEvolutionNeeds& output) const;
  bool readPcPage(PcOrder order, size_t offset, std::span<PokemonRecord> output, size_t& count) const;
  bool commit(const PokemonState& state, const RecordMutation& mutation = {});
  bool reset();

 private:
  bool writeSnapshot(const PokemonState& state, const RecordMutation& mutation, bool discardRecords);

  SnapshotHeader activeHeader_{};
  uint32_t highestRecordId_ = 0;
  bool activeIsA_ = false;
  bool ready_ = false;
  bool writable_ = false;
};

}  // namespace pokemon
