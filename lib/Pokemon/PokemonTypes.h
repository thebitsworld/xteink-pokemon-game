#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace pokemon {

constexpr size_t POKEMON_RECORD_BYTES = 48;
constexpr size_t POKEMON_NICKNAME_BYTES = 33;
constexpr uint16_t KANTO_SPECIES_COUNT = 151;
constexpr uint32_t MAXIMUM_TOTAL_XP = 8340;
constexpr size_t POKEDEX_BYTES = 19;
constexpr size_t PARTY_SIZE = 6;
constexpr size_t EVOLUTION_ITEM_COUNT = 6;
constexpr size_t PENDING_EVENT_CAPACITY = 3;

// Pinned copies of constants that really live in PokemonBattleTypes.h
// (MOVE_COUNT, ITEM_COUNT, GYM_COUNT). PokemonTypes.h/.cpp is the core layer
// that PokemonBattleTypes.h itself depends on (via PokemonSpecies.h), so it
// cannot include the battle layer without inverting that dependency -
// PokemonMoveData.cpp/PokemonItemData.cpp/PokemonGymData.cpp each carry a
// static_assert cross-checking these stay in sync with their source of truth.
constexpr uint16_t POKEMON_MOVE_ID_MAX = 165;
constexpr size_t POKEMON_BAG_SLOT_COUNT = 77;       // ITEM_COUNT(83) - the 6 evolution stones tracked in itemCounts
constexpr uint8_t POKEMON_ITEM_ID_MAX = 83;         // = EVOLUTION_ITEM_COUNT + POKEMON_BAG_SLOT_COUNT
constexpr uint16_t POKEMON_GYM_PROGRESS_BITS = 12;  // 8 gyms + 4 Elite Four
constexpr uint16_t POKEMON_GYM_PROGRESS_MASK = static_cast<uint16_t>((1U << POKEMON_GYM_PROGRESS_BITS) - 1U);

enum class Gender : uint8_t {
  Unknown = 0,
  Male = 1,
  Female = 2,
  Genderless = 3,
};

enum class Origin : uint8_t {
  Unknown = 0,
  Caught = 1,
  Starter = 2,
};

enum class RecordFlag : uint8_t {
  EvolutionPromptsDisabled = 1U << 0U,
};

enum class EvolutionItem : uint8_t {
  None = 0,
  MoonStone = 1,
  FireStone = 2,
  ThunderStone = 3,
  WaterStone = 4,
  LeafStone = 5,
  LinkCable = 6,
};

enum class PendingEventKind : uint8_t {
  None = 0,
  Encounter = 1,
  Item = 2,
  Evolution = 3,
  // Reuses PendingEvent's existing 10-byte layout: `speciesId` holds the
  // moveId being learned (1..POKEMON_MOVE_ID_MAX) and `level` holds the
  // level it was learned at. No size change to PendingEvent/PokemonState.
  MoveLearn = 4,
};

enum class DashboardNotice : uint8_t {
  None = 0,
  NewPokemon = 1,
  ItemFound = 2,
  WhatsThis = 3,
};

constexpr uint8_t recordFlag(const RecordFlag flag) { return static_cast<uint8_t>(flag); }

struct PokemonRecord {
  uint32_t recordId = 0;
  uint32_t totalXp = 0;
  uint16_t speciesId = 0;
  uint8_t caughtLevel = 0;
  Gender gender = Gender::Unknown;
  Origin origin = Origin::Unknown;
  uint8_t flags = 0;
  std::array<char, POKEMON_NICKNAME_BYTES> nickname{};

  bool operator==(const PokemonRecord&) const = default;
};

struct LevelXpProgress {
  uint8_t level = 1;
  uint32_t earned = 0;
  uint32_t required = 0;
};

using RecordBytes = std::array<uint8_t, POKEMON_RECORD_BYTES>;

struct PendingEvent {
  uint32_t recordId = 0;
  uint16_t speciesId = 0;
  uint8_t level = 0;
  Gender gender = Gender::Unknown;
  EvolutionItem item = EvolutionItem::None;
  PendingEventKind kind = PendingEventKind::None;

  bool operator==(const PendingEvent&) const = default;
};

using PokedexBits = std::array<uint8_t, POKEDEX_BYTES>;

struct PokemonState {
  std::array<uint32_t, PARTY_SIZE> partyRecordIds{};
  std::array<PendingEvent, PENDING_EVENT_CAPACITY> pendingEvents{};
  std::array<uint16_t, EVOLUTION_ITEM_COUNT> itemCounts{};
  PokedexBits seenSpecies{};
  PokedexBits caughtSpecies{};
  uint32_t lifetimeMinutes = 0;
  uint32_t sequence = 0;
  uint8_t readingMinuteRemainder = 0;
  uint8_t encounterMisses = 0;
  uint8_t itemMisses = 0;
  DashboardNotice dashboardNotice = DashboardNotice::None;
  // v3: appended after the v2 layout (PokemonStoreCodec.cpp's byte 116) so
  // a v2 save zero-extends cleanly instead of requiring a field shuffle.
  std::array<uint8_t, POKEMON_BAG_SLOT_COUNT> bagCounts{};
  uint16_t battleProgress = 0;  // bit N (0-7) = gym N+1 defeated, bit 8+M (0-3) = Elite Four member M+1 defeated
  // v4: appended after the v3 layout (PokemonStoreCodec.cpp's byte 195). Pity
  // counters for the three item-drop tracks that gained their own
  // guaranteed-after-3-misses roll in GĐ 23 (see PokemonGame.cpp) - mirrors
  // encounterMisses/itemMisses above, just one counter per new track.
  uint8_t ballMisses = 0;
  uint8_t medicineMisses = 0;
  uint8_t machineMisses = 0;

  bool operator==(const PokemonState&) const = default;
};

bool validateNickname(std::string_view nickname);
bool setNickname(PokemonRecord& record, std::string_view nickname);
bool validateRecord(const PokemonRecord& record);
bool encodeRecord(const PokemonRecord& record, RecordBytes& output);
bool decodeRecord(const RecordBytes& bytes, PokemonRecord& output);
uint32_t xpRequired(uint8_t level);
uint8_t levelForXp(uint32_t totalXp);
LevelXpProgress levelXpProgress(uint32_t totalXp);
bool markSpecies(PokedexBits& bits, uint16_t speciesId);
bool isSpeciesMarked(const PokedexBits& bits, uint16_t speciesId);
size_t pendingEventCount(const PokemonState& state);
const PendingEvent* pendingEventFront(const PokemonState& state);
PendingEvent* pendingEventFront(PokemonState& state);
bool enqueuePendingEvent(PokemonState& state, const PendingEvent& event);
bool dequeuePendingEvent(PokemonState& state);
size_t removePendingEvolutionsForRecord(PokemonState& state, uint32_t recordId);
bool validateState(const PokemonState& state);

}  // namespace pokemon
