#if defined(CROSSINK_ENABLE_POKEMON)

#include "PokemonActivity.h"

#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <PokemonSpecies.h>
#include <PokemonUiLayout.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "components/pokemon/PokemonArt.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_ROW = 1;
constexpr uint16_t STARTERS[] = {1, 4, 7, 25};

const char* speciesName(const uint16_t id) {
  const pokemon::SpeciesData* species = pokemon::speciesData(id);
  return species == nullptr ? "???" : species->name;
}

const char* genderText(const pokemon::Gender gender) {
  if (gender == pokemon::Gender::Male) return tr(STR_POKEMON_MALE);
  if (gender == pokemon::Gender::Female) return tr(STR_POKEMON_FEMALE);
  if (gender == pokemon::Gender::Genderless) return tr(STR_POKEMON_GENDERLESS);
  return "";
}

const char* typeName(const pokemon::PokemonType type) {
  switch (type) {
    case pokemon::PokemonType::Normal:
      return tr(STR_POKEMON_TYPE_NORMAL);
    case pokemon::PokemonType::Fire:
      return tr(STR_POKEMON_TYPE_FIRE);
    case pokemon::PokemonType::Water:
      return tr(STR_POKEMON_TYPE_WATER);
    case pokemon::PokemonType::Electric:
      return tr(STR_POKEMON_TYPE_ELECTRIC);
    case pokemon::PokemonType::Grass:
      return tr(STR_POKEMON_TYPE_GRASS);
    case pokemon::PokemonType::Ice:
      return tr(STR_POKEMON_TYPE_ICE);
    case pokemon::PokemonType::Fighting:
      return tr(STR_POKEMON_TYPE_FIGHTING);
    case pokemon::PokemonType::Poison:
      return tr(STR_POKEMON_TYPE_POISON);
    case pokemon::PokemonType::Ground:
      return tr(STR_POKEMON_TYPE_GROUND);
    case pokemon::PokemonType::Flying:
      return tr(STR_POKEMON_TYPE_FLYING);
    case pokemon::PokemonType::Psychic:
      return tr(STR_POKEMON_TYPE_PSYCHIC);
    case pokemon::PokemonType::Bug:
      return tr(STR_POKEMON_TYPE_BUG);
    case pokemon::PokemonType::Rock:
      return tr(STR_POKEMON_TYPE_ROCK);
    case pokemon::PokemonType::Ghost:
      return tr(STR_POKEMON_TYPE_GHOST);
    case pokemon::PokemonType::Dragon:
      return tr(STR_POKEMON_TYPE_DRAGON);
    case pokemon::PokemonType::Dark:
      return tr(STR_POKEMON_TYPE_DARK);
    case pokemon::PokemonType::Steel:
      return tr(STR_POKEMON_TYPE_STEEL);
    case pokemon::PokemonType::Fairy:
      return tr(STR_POKEMON_TYPE_FAIRY);
    case pokemon::PokemonType::None:
      return "";
  }
  return "";
}

const char* itemName(const pokemon::EvolutionItem item) {
  switch (item) {
    case pokemon::EvolutionItem::MoonStone:
      return tr(STR_POKEMON_MOON_STONE);
    case pokemon::EvolutionItem::FireStone:
      return tr(STR_POKEMON_FIRE_STONE);
    case pokemon::EvolutionItem::ThunderStone:
      return tr(STR_POKEMON_THUNDER_STONE);
    case pokemon::EvolutionItem::WaterStone:
      return tr(STR_POKEMON_WATER_STONE);
    case pokemon::EvolutionItem::LeafStone:
      return tr(STR_POKEMON_LEAF_STONE);
    case pokemon::EvolutionItem::LinkCable:
      return tr(STR_POKEMON_LINK_CABLE);
    default: {
      // GĐ 4 widened PendingEventKind::Item to hold any of the 83 items, not
      // just the original 6 evolution stones - those extra ids have no
      // i18n string (move/item names are intentionally not localized, see
      // the roadmap) and instead come straight from the generated data.
      const pokemon::ItemData* data = pokemon::itemData(static_cast<uint8_t>(item));
      return data == nullptr ? "" : data->name;
    }
  }
}

// The Bag is split into 3 categories, each its own screen: Evolution (the
// 6 stones + Link Cable, fixed positions unchanged since GĐ 1), Medicine
// (heal/status-cure/PP-restore/candy items - GĐ 9), and Machine (TM/HM,
// GĐ 7). Ball items have no Bag row at all - they are only ever consumed
// via BattleBalls. Ids are not contiguous by category in the data file, so
// these walk the table by predicate rather than assuming a fixed range.
bool isMachineCategory(const pokemon::ItemCategory category) { return category == pokemon::ItemCategory::Machine; }

bool isMedicineCategory(const pokemon::ItemCategory category) {
  return category == pokemon::ItemCategory::Medicine || category == pokemon::ItemCategory::StatusCure ||
         category == pokemon::ItemCategory::PPRestore || category == pokemon::ItemCategory::Candy;
}

uint8_t bagItemIdAt(const size_t index, bool (*matches)(pokemon::ItemCategory)) {
  size_t count = 0;
  for (uint8_t id = pokemon::EVOLUTION_ITEM_COUNT + 1U; id <= pokemon::POKEMON_ITEM_ID_MAX; ++id) {
    const pokemon::ItemData* data = pokemon::itemData(id);
    if (data == nullptr || !matches(data->category)) continue;
    if (count == index) return id;
    ++count;
  }
  return 0;
}

size_t bagItemCount(bool (*matches)(pokemon::ItemCategory)) {
  size_t count = 0;
  for (uint8_t id = pokemon::EVOLUTION_ITEM_COUNT + 1U; id <= pokemon::POKEMON_ITEM_ID_MAX; ++id) {
    const pokemon::ItemData* data = pokemon::itemData(id);
    if (data != nullptr && matches(data->category)) ++count;
  }
  return count;
}

bool moveIsKnown(const pokemon::BattleRecordEntry& entry, const uint8_t moveId) {
  for (size_t slot = 0; slot < pokemon::BATTLE_MOVE_SLOTS; ++slot) {
    if (entry.moves[slot] == moveId) return true;
  }
  return false;
}

// The Moveset screen (Party > Actions > Moves) lets the player freely swap
// in any move their Pokemon's own learnset has already unlocked at its
// current level but isn't currently using - not just the one move offered
// automatically at the moment of leveling up (PendingEventKind::MoveLearn).
uint8_t learnableMoveIdAt(const uint16_t speciesId, const uint8_t level, const pokemon::BattleRecordEntry& known,
                          const size_t index) {
  size_t count = 0;
  for (const pokemon::LearnsetEntry& entry : pokemon::learnsetFor(speciesId)) {
    if (entry.level > level || moveIsKnown(known, entry.moveId)) continue;
    if (count == index) return entry.moveId;
    ++count;
  }
  return 0;
}

size_t learnableMoveCount(const uint16_t speciesId, const uint8_t level, const pokemon::BattleRecordEntry& known) {
  size_t count = 0;
  for (const pokemon::LearnsetEntry& entry : pokemon::learnsetFor(speciesId)) {
    if (entry.level <= level && !moveIsKnown(known, entry.moveId)) ++count;
  }
  return count;
}

// Short status-abbreviation tags (PSN/PAR/...), intentionally not
// localized - like move and item names (GĐ 1 decision), these read the same
// in every official Pokémon localization, so translating them would spend
// i18n budget without actually helping a non-English player.
const char* statusAbbrev(const pokemon::Ailment status) {
  switch (status) {
    case pokemon::Ailment::Paralysis:
      return "PAR";
    case pokemon::Ailment::Sleep:
      return "SLP";
    case pokemon::Ailment::Freeze:
      return "FRZ";
    case pokemon::Ailment::Burn:
      return "BRN";
    case pokemon::Ailment::Poison:
      return "PSN";
    case pokemon::Ailment::Confusion:
      return "CNF";
    case pokemon::Ailment::None:
    case pokemon::Ailment::All:
      return "";
  }
  return "";
}

// Builds one side's battle-log line for the turn just resolved. Status-only
// events (couldn't move, confusion self-hit, cured, status damage, fainted)
// skip the "used MOVE!" framing since no move was actually executed.
void formatBattleActionLine(char* buffer, const size_t size, const pokemon::BattleCombatant& actor,
                            const pokemon::BattleActionResult& action) {
  const char* name = speciesName(actor.speciesId);
  switch (action.event) {
    case pokemon::BattleLogEvent::None:
    case pokemon::BattleLogEvent::MoveHadNoPp:
      buffer[0] = '\0';
      return;
    case pokemon::BattleLogEvent::Fainted:
      snprintf(buffer, size, tr(STR_POKEMON_FAINTED), name);
      return;
    case pokemon::BattleLogEvent::StatusPreventedMove:
    case pokemon::BattleLogEvent::ConfusionSelfHit:
    case pokemon::BattleLogEvent::StatusCured:
    case pokemon::BattleLogEvent::StatusDamage: {
      const char* suffix =
          action.event == pokemon::BattleLogEvent::StatusPreventedMove ? tr(STR_POKEMON_STATUS_PREVENTED)
          : action.event == pokemon::BattleLogEvent::ConfusionSelfHit  ? tr(STR_POKEMON_CONFUSION_HURT_SELF)
          : action.event == pokemon::BattleLogEvent::StatusCured       ? tr(STR_POKEMON_STATUS_CURED)
                                                                       : tr(STR_POKEMON_STATUS_DAMAGE);
      snprintf(buffer, size, "%s %s", name, suffix);
      return;
    }
    default:
      break;
  }
  const pokemon::MoveData* move = pokemon::moveData(actor.moves[action.moveSlot].moveId);
  char used[64];
  snprintf(used, sizeof(used), tr(STR_POKEMON_USED_MOVE), name, move == nullptr ? "?" : move->name);
  const char* suffix = action.event == pokemon::BattleLogEvent::MoveMissed           ? tr(STR_POKEMON_MOVE_MISSED)
                       : action.event == pokemon::BattleLogEvent::MoveNoEffect       ? tr(STR_POKEMON_NO_EFFECT)
                       : action.event == pokemon::BattleLogEvent::MoveSuperEffective ? tr(STR_POKEMON_SUPER_EFFECTIVE)
                       : action.event == pokemon::BattleLogEvent::MoveNotVeryEffective
                           ? tr(STR_POKEMON_NOT_VERY_EFFECTIVE)
                       : action.event == pokemon::BattleLogEvent::InflictedStatus ? tr(STR_POKEMON_INFLICTED_STATUS)
                                                                                  : "";
  if (suffix[0] == '\0') {
    snprintf(buffer, size, "%s", used);
  } else {
    snprintf(buffer, size, "%s %s", used, suffix);
  }
}

void centered(const GfxRenderer& renderer, const int font, const int y, const char* text,
              const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  if (const char* newline = strchr(text, '\n')) {
    char first[64];
    snprintf(first, sizeof(first), "%.*s", static_cast<int>(newline - text), text);
    centered(renderer, font, y, first, style);
    centered(renderer, font, y + 28, newline + 1, style);
    return;
  }
  renderer.drawText(font, (renderer.getScreenWidth() - renderer.getTextWidth(font, text, style)) / 2, y, text, true,
                    style);
}
}  // namespace

PokemonActivity::PokemonActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Pokemon", renderer, mappedInput),
      service_(pokemon::devicePokemonService()),
      uiTarget_(makeUiTarget(renderer)),
      app_(uiTarget_, uiTarget_.deviceContext()) {}

void PokemonActivity::onEnter() {
  Activity::onEnter();
  app_.setTheme(uiThemeTokens(uiTarget_));
  app_.on(ACTION_ROW, &PokemonActivity::onRow, this);
  loadInitialScreen();
}

void PokemonActivity::loadInitialScreen() {
  const pokemon::ServiceStatus status = service_.loadSnapshot(snapshot_);
  if (status == pokemon::ServiceStatus::Empty) {
    setScreen(Screen::Starter);
  } else if (status != pokemon::ServiceStatus::Ok) {
    showMessage(tr(STR_POKEMON_SAVE_ERROR), Screen::Menu);
  } else {
    setScreen(pokemon::pendingEventFront(snapshot_.state) == nullptr ? Screen::Menu : Screen::Event);
  }
}

bool PokemonActivity::refreshSnapshot() {
  if (service_.loadSnapshot(snapshot_) != pokemon::ServiceStatus::Ok) {
    showMessage(tr(STR_POKEMON_SAVE_ERROR), Screen::Menu);
    return false;
  }
  return true;
}

void PokemonActivity::setScreen(const Screen screen, const int selected) {
  cleanRefreshNeeded_ =
      cleanRefreshNeeded_ ||
      pokemon::pokemonNeedsCleanRefresh(screen_ == Screen::PokedexDetail, screen == Screen::PokedexDetail, false);
  screen_ = screen;
  selected_ = std::max(0, selected);
  uiReady_ = false;
  app_.setScreen(&PokemonActivity::screenBuilder, this);
  requestUpdate();
}

bool PokemonActivity::isListScreen() const {
  return screen_ != Screen::Summary && screen_ != Screen::PokedexDetail && screen_ != Screen::Message;
}

int PokemonActivity::logicalCount() const {
  switch (screen_) {
    case Screen::Starter:
      return 4;
    case Screen::Gender:
    case Screen::NicknameQuestion:
    case Screen::Move:
    case Screen::ResetFirst:
    case Screen::ResetFinal:
      return screen_ == Screen::Move ? snapshot_.partyCount : 2;
    case Screen::Menu:
      return 9;
    case Screen::Party:
      return pokemon::PARTY_SIZE;
    case Screen::Actions: {
      const auto actions = pokemon::collectionActions(actionSource_ == Screen::Party, snapshot_.partyCount);
      return actions.count;
    }
    case Screen::Moveset:
      return pokemon::BATTLE_MOVE_SLOTS;
    case Screen::MovesetPick: {
      pokemon::PokemonRecord record{};
      if (service_.readRecord(focusedRecordId_, record) != pokemon::ServiceStatus::Ok) return 0;
      const pokemon::BattleRecordEntry entry = service_.peekBattleMoves(record);
      // +1 for the trailing "Forget" row - always offered regardless of
      // whether there's anything new to learn (GĐ12).
      return static_cast<int>(learnableMoveCount(record.speciesId, pokemon::levelForXp(record.totalXp), entry)) + 1;
    }
    case Screen::TmReplaceSlot:
      return pokemon::BATTLE_MOVE_SLOTS + 1;
    case Screen::Pc:
      return static_cast<int>(snapshot_.ownedCount - snapshot_.partyCount);
    case Screen::PcOrder:
      return 3;
    case Screen::Bag:
      return 3;
    case Screen::BagEvolution:
      return pokemon::EVOLUTION_ITEM_COUNT;
    case Screen::BagMedicine:
      return static_cast<int>(bagItemCount(isMedicineCategory));
    case Screen::BagMachine:
      return static_cast<int>(bagItemCount(isMachineCategory));
    case Screen::ItemTarget:
      return snapshot_.partyCount;
    case Screen::Pokedex:
      return pokemon::KANTO_SPECIES_COUNT;
    case Screen::Event: {
      const pokemon::PendingEvent* pending = pokemon::pendingEventFront(snapshot_.state);
      if (pending == nullptr) return 2;
      if (pending->kind == pokemon::PendingEventKind::Item) return 1;
      if (pending->kind == pokemon::PendingEventKind::MoveLearn) return pokemon::BATTLE_MOVE_SLOTS + 1;
      return 2;
    }
    case Screen::Battle:
      return gymChallengeIndex_ != 0 ? 2 : 3;  // trainer battles have no BALL option
    case Screen::BattleMoves:
      return battlePlayerMoveCount();
    case Screen::BattleBalls:
      return 4;
    case Screen::GymList:
      return pokemon::GYM_COUNT;
    case Screen::Badges:
      return 8;  // gym badges only - Elite Four members award no badge
    case Screen::Summary:
    case Screen::PokedexDetail:
    case Screen::Message:
      return 0;
  }
  return 0;
}

int PokemonActivity::listTop() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  int top = metrics.topPadding + TouchHeaderBackButton::height(metrics, mappedInput) + metrics.verticalSpacing;
  if (screen_ == Screen::Starter || screen_ == Screen::ResetFirst || screen_ == Screen::ResetFinal) top += 72;
  if (screen_ == Screen::Gender) top += 180;
  if (screen_ == Screen::NicknameQuestion) top += 210;
  return top;
}

int PokemonActivity::rowsPerPage() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  constexpr int rowHeight = 64;
  const int bottomReserve = metrics.buttonHintsHeight + 8;
  return pokemon::pokemonRowsPerPage(renderer.getScreenHeight(), listTop(), bottomReserve, rowHeight, ROW_CAPACITY);
}

int PokemonActivity::pageStart() const { return pokemon::pokemonPageStart(selected_, rowsPerPage()); }

uint32_t PokemonActivity::selectedRecordId() const {
  if (screen_ == Screen::Party || screen_ == Screen::Move || screen_ == Screen::ItemTarget) {
    return selected_ < snapshot_.partyCount ? snapshot_.party[selected_].recordId : 0;
  }
  if (screen_ == Screen::Pc) {
    const int local = selected_ - pageStart();
    return local >= 0 && local < static_cast<int>(pcCount_) ? pcPage_[local].recordId : 0;
  }
  return focusedRecordId_;
}

void PokemonActivity::showMessage(const char* message, const Screen returnScreen) {
  snprintf(message_, sizeof(message_), "%s", message == nullptr ? "" : message);
  returnScreen_ = returnScreen;
  setScreen(Screen::Message);
}

void PokemonActivity::finishStarter(const char* nickname) {
  const auto status = service_.createStarter(starterSpecies_, starterGender_, nickname == nullptr ? "" : nickname);
  if (status != pokemon::ServiceStatus::Ok) {
    showMessage(tr(STR_POKEMON_SAVE_ERROR), Screen::Starter);
    return;
  }
  if (!refreshSnapshot()) return;
  setScreen(Screen::Menu);
}

void PokemonActivity::openNickname(const uint32_t recordId, const bool starter, const Screen cancelScreen) {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_POKEMON_NICKNAME), "", 32,
                                                           InputType::Text);
  if (!keyboard) {
    LOG_ERR("PokemonActivity", "Could not allocate nickname keyboard");
    showMessage(tr(STR_POKEMON_SAVE_ERROR), cancelScreen);
    return;
  }
  startActivityForResult(std::move(keyboard), [this, recordId, starter, cancelScreen](const ActivityResult& result) {
    if (result.isCancelled) {
      setScreen(cancelScreen);
      return;
    }
    const auto* keyboardResult = std::get_if<KeyboardResult>(&result.data);
    if (keyboardResult == nullptr) return;
    if (starter) {
      finishStarter(keyboardResult->text.c_str());
    } else if (service_.renamePokemon(recordId, keyboardResult->text) == pokemon::ServiceStatus::Ok) {
      if (!refreshSnapshot()) return;
      nicknamePrompt_ = {};
      setScreen(pokemon::pendingEventFront(snapshot_.state) == nullptr ? Screen::Menu : Screen::Event);
    } else {
      showMessage(tr(STR_POKEMON_SAVE_ERROR), Screen::Menu);
    }
  });
}

int PokemonActivity::battlePlayerMoveCount() const {
  int count = 0;
  while (count < static_cast<int>(pokemon::BATTLE_MOVE_SLOTS) && battlePlayer_.moves[count].moveId != 0) ++count;
  return count;
}

bool PokemonActivity::setupBattlePlayer() {
  if (snapshot_.partyCount == 0) return false;
  const pokemon::PokemonRecord& leader = snapshot_.party[0];
  pokemon::BattleRecordEntry entry{};
  if (service_.loadBattleEntry(leader.recordId, entry) != pokemon::ServiceStatus::Ok) return false;

  battlePlayer_ = pokemon::BattleCombatant{};
  battlePlayer_.speciesId = leader.speciesId;
  battlePlayer_.level = pokemon::levelForXp(leader.totalXp);
  const pokemon::BaseStats* playerStats = pokemon::baseStatsFor(leader.speciesId);
  battlePlayer_.maxHp = playerStats == nullptr ? 1 : pokemon::battleMaxHp(playerStats->hp, battlePlayer_.level);
  battlePlayer_.currentHp = std::min<uint16_t>(entry.currentHp, battlePlayer_.maxHp);
  battlePlayer_.status = entry.status;
  battlePlayer_.statusTurns = entry.statusTurns;
  for (size_t i = 0; i < pokemon::BATTLE_MOVE_SLOTS; ++i) {
    battlePlayer_.moves[i] = pokemon::BattleMoveSlot{entry.moves[i], entry.pp[i]};
  }
  return true;
}

void PokemonActivity::setupBattleOpponent(const uint16_t speciesId, const uint8_t level,
                                          const std::span<const uint8_t> fixedMoves) {
  battleOpponent_ = pokemon::BattleCombatant{};
  battleOpponent_.speciesId = speciesId;
  battleOpponent_.level = level;
  const pokemon::BaseStats* stats = pokemon::baseStatsFor(speciesId);
  battleOpponent_.maxHp = stats == nullptr ? 1 : pokemon::battleMaxHp(stats->hp, level);
  battleOpponent_.currentHp = battleOpponent_.maxHp;
  if (!fixedMoves.empty()) {
    // Gym/Elite Four trainer - real Pokemon Red teams never derive their
    // moves from the learnset-by-level table, so this comes straight from
    // GymTeamMember::moves (GĐ12) instead of defaultMovesetForLevel().
    for (size_t i = 0; i < pokemon::BATTLE_MOVE_SLOTS; ++i) {
      const uint8_t moveId = i < fixedMoves.size() ? fixedMoves[i] : 0;
      const pokemon::MoveData* move = pokemon::moveData(moveId);
      battleOpponent_.moves[i] = pokemon::BattleMoveSlot{moveId, move == nullptr ? 0 : move->pp};
    }
    return;
  }
  std::array<uint8_t, pokemon::BATTLE_MOVE_SLOTS> moveIds{};
  std::array<uint8_t, pokemon::BATTLE_MOVE_SLOTS> pp{};
  pokemon::defaultMovesetForLevel(speciesId, level, moveIds, pp);
  for (size_t i = 0; i < pokemon::BATTLE_MOVE_SLOTS; ++i) {
    battleOpponent_.moves[i] = pokemon::BattleMoveSlot{moveIds[i], pp[i]};
  }
}

bool PokemonActivity::enterBattle(const pokemon::PendingEvent& pending) {
  if (!setupBattlePlayer()) return false;
  setupBattleOpponent(pending.speciesId, pending.level);
  gymChallengeIndex_ = 0;
  battleLog_[0] = '\0';
  setScreen(Screen::Battle);
  return true;
}

bool PokemonActivity::enterGymBattle(const uint8_t gymIndex) {
  const auto team = pokemon::gymTeamFor(gymIndex);
  if (team.empty()) return false;
  if (!setupBattlePlayer()) return false;
  setupBattleOpponent(team[0].speciesId, team[0].level, team[0].moves);
  gymChallengeIndex_ = gymIndex;
  gymChallengeTeamProgress_ = 0;
  battleLog_[0] = '\0';
  setScreen(Screen::Battle);
  return true;
}

void PokemonActivity::savePlayerBattleEntry() {
  if (snapshot_.partyCount == 0) return;
  pokemon::BattleRecordEntry entry{};
  entry.recordId = snapshot_.party[0].recordId;
  for (size_t i = 0; i < pokemon::BATTLE_MOVE_SLOTS; ++i) {
    entry.moves[i] = battlePlayer_.moves[i].moveId;
    entry.pp[i] = battlePlayer_.moves[i].currentPp;
  }
  entry.currentHp = battlePlayer_.currentHp;
  entry.status = battlePlayer_.status;
  entry.statusTurns = battlePlayer_.statusTurns;
  // Best-effort: the in-memory battle state already reflects reality either
  // way, and a rare SD write failure here shouldn't block the player from
  // continuing the fight or leaving it.
  service_.saveBattleEntry(entry);
}

void PokemonActivity::resolveBattleAsPass() {
  savePlayerBattleEntry();
  if (gymChallengeIndex_ != 0) {
    // Forfeiting a gym challenge (RUN, or hardware Back) costs nothing and
    // records nothing - unlike a wild encounter there is no PendingEvent to
    // clear, so this just drops the in-progress challenge silently.
    gymChallengeIndex_ = 0;
    gymChallengeTeamProgress_ = 0;
    setScreen(Screen::GymList);
    return;
  }
  uint32_t caught = 0;
  if (service_.resolveEncounter(pokemon::EncounterChoice::Pass, caught) != pokemon::ServiceStatus::Ok) {
    showMessage(tr(STR_POKEMON_SAVE_ERROR), Screen::Menu);
    return;
  }
  if (!refreshSnapshot()) return;
  setScreen(pokemon::pendingEventFront(snapshot_.state) == nullptr ? Screen::Menu : Screen::Event);
}

void PokemonActivity::finishBattleAfterWildFainted() {
  if (gymChallengeIndex_ != 0) {
    advanceGymOpponentOrFinish();
    return;
  }
  const uint16_t opponentSpecies = battleOpponent_.speciesId;
  uint32_t caught = 0;
  // The wild Pokemon fainted from the fight - nothing left to catch. Pass
  // clears the pending encounter without creating a record, same as always.
  service_.resolveEncounter(pokemon::EncounterChoice::Pass, caught);
  if (!refreshSnapshot()) return;
  char line[96];
  snprintf(line, sizeof(line), tr(STR_POKEMON_FAINTED), speciesName(opponentSpecies));
  showMessage(line, pokemon::pendingEventFront(snapshot_.state) == nullptr ? Screen::Menu : Screen::Event);
}

void PokemonActivity::finishBattleAfterPlayerFainted() {
  if (gymChallengeIndex_ != 0) {
    finishGymChallenge(false);
    return;
  }
  const uint16_t playerSpecies = battlePlayer_.speciesId;
  uint32_t caught = 0;
  service_.resolveEncounter(pokemon::EncounterChoice::Pass, caught);
  if (!refreshSnapshot()) return;
  char line[96];
  snprintf(line, sizeof(line), tr(STR_POKEMON_YOUR_POKEMON_FAINTED), speciesName(playerSpecies));
  showMessage(line, pokemon::pendingEventFront(snapshot_.state) == nullptr ? Screen::Menu : Screen::Event);
}

void PokemonActivity::advanceGymOpponentOrFinish() {
  const auto team = pokemon::gymTeamFor(gymChallengeIndex_);
  ++gymChallengeTeamProgress_;
  if (gymChallengeTeamProgress_ < team.size()) {
    const auto& next = team[gymChallengeTeamProgress_];
    setupBattleOpponent(next.speciesId, next.level, next.moves);
    setScreen(Screen::Battle);
    return;
  }
  finishGymChallenge(true);
}

void PokemonActivity::finishGymChallenge(const bool won) {
  const uint8_t gymIndex = gymChallengeIndex_;
  gymChallengeIndex_ = 0;
  gymChallengeTeamProgress_ = 0;
  if (!won) {
    if (!refreshSnapshot()) return;
    showMessage(tr(STR_POKEMON_GYM_CHALLENGE_LOST), Screen::GymList);
    return;
  }
  if (service_.markGymDefeated(gymIndex) != pokemon::ServiceStatus::Ok) {
    showMessage(tr(STR_POKEMON_SAVE_ERROR), Screen::GymList);
    return;
  }
  const pokemon::GymData* gym = pokemon::gymData(gymIndex);
  const char* leaderName = gym == nullptr ? "?" : gym->leaderName;
  char line[96];
  if (gym != nullptr && gym->badgeName[0] != '\0') {
    snprintf(line, sizeof(line), tr(STR_POKEMON_BADGE_EARNED), leaderName, gym->badgeName);
  } else {
    snprintf(line, sizeof(line), tr(STR_POKEMON_TRAINER_DEFEATED), leaderName);
  }
  if (!refreshSnapshot()) return;
  showMessage(line, Screen::GymList);
}

void PokemonActivity::buildBattleLog(const pokemon::BattleTurnResult& result) {
  char playerLine[80] = "";
  char opponentLine[80] = "";
  if (result.player.acted) formatBattleActionLine(playerLine, sizeof(playerLine), battlePlayer_, result.player);
  if (result.opponent.acted)
    formatBattleActionLine(opponentLine, sizeof(opponentLine), battleOpponent_, result.opponent);
  if (playerLine[0] != '\0' && opponentLine[0] != '\0') {
    snprintf(battleLog_, sizeof(battleLog_), "%s\n%s", playerLine, opponentLine);
  } else if (playerLine[0] != '\0') {
    snprintf(battleLog_, sizeof(battleLog_), "%s", playerLine);
  } else {
    snprintf(battleLog_, sizeof(battleLog_), "%s", opponentLine);
  }
}

void PokemonActivity::activate() {
  switch (screen_) {
    case Screen::Starter:
      starterSpecies_ = STARTERS[selected_];
      setScreen(Screen::Gender);
      return;
    case Screen::Gender:
      starterGender_ = selected_ == 0 ? pokemon::Gender::Male : pokemon::Gender::Female;
      nicknamePrompt_ = pokemon::PokemonPromptContext::forStarter(starterSpecies_);
      snprintf(message_, sizeof(message_), tr(STR_POKEMON_NICKNAME_QUESTION), speciesName(starterSpecies_));
      setScreen(Screen::NicknameQuestion);
      return;
    case Screen::NicknameQuestion:
      if (selected_ == 0) {
        openNickname(nicknamePrompt_.recordId, nicknamePrompt_.isStarter(), Screen::NicknameQuestion);
      } else if (nicknamePrompt_.isStarter())
        finishStarter("");
      else {
        nicknamePrompt_ = {};
        setScreen(pokemon::pendingEventFront(snapshot_.state) == nullptr ? Screen::Menu : Screen::Event);
      }
      return;
    case Screen::Menu:
      if (selected_ == 0)
        setScreen(Screen::Party);
      else if (selected_ == 1)
        setScreen(Screen::Pokedex);
      else if (selected_ == 2)
        setScreen(Screen::Pc);
      else if (selected_ == 3)
        setScreen(Screen::PcOrder, static_cast<int>(pcOrder_));
      else if (selected_ == 4)
        setScreen(Screen::Bag);
      else if (selected_ == 5)
        setScreen(Screen::GymList);
      else if (selected_ == 6)
        setScreen(Screen::Badges);
      else if (selected_ == 7) {
        const uint8_t previous = SETTINGS.pokemonHomeScreen;
        SETTINGS.pokemonHomeScreen = previous == 0 ? 1 : 0;
        if (!SETTINGS.saveToFile()) {
          SETTINGS.pokemonHomeScreen = previous;
          showMessage(tr(STR_POKEMON_SAVE_ERROR), Screen::Menu);
        } else {
          setScreen(Screen::Menu, selected_);
        }
      } else
        setScreen(Screen::ResetFirst);
      return;
    case Screen::Party:
    case Screen::Pc: {
      const uint32_t recordId = selectedRecordId();
      if (recordId == 0) return;
      focusedRecordId_ = recordId;
      actionSource_ = screen_;
      setScreen(Screen::Actions);
      return;
    }
    case Screen::Summary:
      setScreen(Screen::Actions);
      return;
    case Screen::Actions: {
      const bool party = actionSource_ == Screen::Party;
      const auto actions = pokemon::collectionActions(party, snapshot_.partyCount);
      if (selected_ < 0 || selected_ >= actions.count) return;
      switch (actions.items[selected_]) {
        case pokemon::CollectionAction::Summary:
          setScreen(Screen::Summary);
          return;
        case pokemon::CollectionAction::Moveset:
          setScreen(Screen::Moveset);
          return;
        case pokemon::CollectionAction::Move: {
          int slot = 0;
          while (slot < snapshot_.partyCount && snapshot_.party[slot].recordId != focusedRecordId_) ++slot;
          setScreen(Screen::Move, slot);
          return;
        }
        case pokemon::CollectionAction::Deposit:
        case pokemon::CollectionAction::Withdraw: {
          const auto status =
              party ? service_.depositPokemon(focusedRecordId_) : service_.withdrawPokemon(focusedRecordId_);
          if (status == pokemon::ServiceStatus::LastPokemon)
            showMessage(tr(STR_POKEMON_LAST_PARTY), actionSource_);
          else if (status == pokemon::ServiceStatus::PartyFull)
            showMessage(tr(STR_POKEMON_PARTY_FULL), actionSource_);
          else if (status != pokemon::ServiceStatus::Ok)
            showMessage(tr(STR_POKEMON_SAVE_ERROR), actionSource_);
          else {
            if (!refreshSnapshot()) return;
            setScreen(actionSource_);
          }
          return;
        }
        case pokemon::CollectionAction::Rename:
          openNickname(focusedRecordId_, false, Screen::Actions);
          return;
        case pokemon::CollectionAction::EvolutionPrompts: {
          pokemon::PokemonRecord record{};
          if (service_.readRecord(focusedRecordId_, record) != pokemon::ServiceStatus::Ok) return;
          const bool enabled = (record.flags & pokemon::recordFlag(pokemon::RecordFlag::EvolutionPromptsDisabled)) == 0;
          if (service_.setEvolutionPrompts(focusedRecordId_, !enabled) != pokemon::ServiceStatus::Ok) {
            showMessage(tr(STR_POKEMON_SAVE_ERROR), Screen::Actions);
          } else {
            if (!refreshSnapshot()) return;
            setScreen(Screen::Summary);
          }
          return;
        }
      }
      break;
    }
    case Screen::Move: {
      int from = 0;
      while (from < snapshot_.partyCount && snapshot_.party[from].recordId != focusedRecordId_) ++from;
      const auto status = service_.movePartyMember(static_cast<uint8_t>(from), static_cast<uint8_t>(selected_));
      if (status != pokemon::ServiceStatus::Ok)
        showMessage(tr(STR_POKEMON_SAVE_ERROR), Screen::Party);
      else {
        if (!refreshSnapshot()) return;
        setScreen(Screen::Party);
      }
      return;
    }
    case Screen::Moveset:
      movesetSlot_ = static_cast<uint8_t>(selected_);
      setScreen(Screen::MovesetPick);
      return;
    case Screen::MovesetPick: {
      pokemon::PokemonRecord record{};
      if (service_.readRecord(focusedRecordId_, record) != pokemon::ServiceStatus::Ok) return;
      const pokemon::BattleRecordEntry entry = service_.peekBattleMoves(record);
      const size_t learnable = learnableMoveCount(record.speciesId, pokemon::levelForXp(record.totalXp), entry);
      if (static_cast<size_t>(selected_) >= learnable) {
        // Trailing "Forget" row - clears the slot instead of learning
        // anything (GĐ12; refuses to clear a Pokemon's last move).
        if (service_.forgetMove(focusedRecordId_, movesetSlot_) != pokemon::ServiceStatus::Ok) {
          showMessage(tr(STR_POKEMON_NOT_APPLICABLE), Screen::Moveset);
          return;
        }
        setScreen(Screen::Moveset);
        return;
      }
      const uint8_t moveId = learnableMoveIdAt(record.speciesId, pokemon::levelForXp(record.totalXp), entry,
                                               static_cast<size_t>(selected_));
      if (moveId == 0) return;
      if (service_.learnMoveIntoSlot(focusedRecordId_, movesetSlot_, moveId) != pokemon::ServiceStatus::Ok) {
        showMessage(tr(STR_POKEMON_SAVE_ERROR), Screen::Moveset);
        return;
      }
      setScreen(Screen::Moveset);
      return;
    }
    case Screen::PcOrder:
      pcOrder_ = static_cast<pokemon::PcOrder>(selected_);
      setScreen(Screen::Pc);
      return;
    case Screen::Bag:
      setScreen(selected_ == 0 ? Screen::BagEvolution : selected_ == 1 ? Screen::BagMedicine : Screen::BagMachine);
      return;
    case Screen::BagEvolution:
      bagCategory_ = BagCategory::Evolution;
      selectedItem_ = static_cast<pokemon::EvolutionItem>(selected_ + 1);
      if (snapshot_.state.itemCounts[selected_] == 0) {
        showMessage(tr(STR_POKEMON_NOT_APPLICABLE), Screen::BagEvolution);
        return;
      }
      setScreen(Screen::ItemTarget);
      return;
    case Screen::BagMedicine: {
      bagCategory_ = BagCategory::Medicine;
      const uint8_t itemId = bagItemIdAt(static_cast<size_t>(selected_), isMedicineCategory);
      const auto bagIndex = static_cast<size_t>(itemId - pokemon::EVOLUTION_ITEM_COUNT - 1U);
      if (itemId == 0 || bagIndex >= snapshot_.state.bagCounts.size() || snapshot_.state.bagCounts[bagIndex] == 0) {
        showMessage(tr(STR_POKEMON_NOT_APPLICABLE), Screen::BagMedicine);
        return;
      }
      selectedMedicineItemId_ = itemId;
      setScreen(Screen::ItemTarget);
      return;
    }
    case Screen::BagMachine: {
      bagCategory_ = BagCategory::Machine;
      const uint8_t itemId = bagItemIdAt(static_cast<size_t>(selected_), isMachineCategory);
      const auto bagIndex = static_cast<size_t>(itemId - pokemon::EVOLUTION_ITEM_COUNT - 1U);
      if (itemId == 0 || bagIndex >= snapshot_.state.bagCounts.size() || snapshot_.state.bagCounts[bagIndex] == 0) {
        showMessage(tr(STR_POKEMON_NOT_APPLICABLE), Screen::BagMachine);
        return;
      }
      const pokemon::ItemData* data = pokemon::itemData(itemId);
      selectedMachineItemId_ = itemId;
      selectedMachineMoveId_ = data == nullptr ? 0 : data->teachesMoveId;
      setScreen(Screen::ItemTarget);
      return;
    }
    case Screen::ItemTarget: {
      const uint32_t recordId = selectedRecordId();
      const Screen bagScreen = bagCategory_ == BagCategory::Evolution  ? Screen::BagEvolution
                               : bagCategory_ == BagCategory::Medicine ? Screen::BagMedicine
                                                                       : Screen::BagMachine;
      if (bagCategory_ == BagCategory::Machine) {
        const pokemon::TeachMoveOutcome outcome = service_.teachMove(recordId, selectedMachineMoveId_);
        if (outcome == pokemon::TeachMoveOutcome::AlreadyKnown) {
          showMessage(tr(STR_POKEMON_ALREADY_KNOWS_MOVE), bagScreen);
        } else if (outcome == pokemon::TeachMoveOutcome::Incompatible) {
          showMessage(tr(STR_POKEMON_CANNOT_LEARN_MACHINE), bagScreen);
        } else if (outcome == pokemon::TeachMoveOutcome::MovesetFull) {
          // Full moveset no longer just blocks the TM - let the player
          // choose which of the 4 current moves to overwrite (GĐ12).
          focusedRecordId_ = recordId;
          setScreen(Screen::TmReplaceSlot);
        } else if (outcome != pokemon::TeachMoveOutcome::Learned) {
          showMessage(tr(STR_POKEMON_SAVE_ERROR), bagScreen);
        } else if (service_.consumeBagItem(selectedMachineItemId_) != pokemon::ServiceStatus::Ok) {
          showMessage(tr(STR_POKEMON_SAVE_ERROR), bagScreen);
        } else if (refreshSnapshot()) {
          setScreen(Screen::Party);
        }
        return;
      }
      if (bagCategory_ == BagCategory::Medicine) {
        const pokemon::UseConsumableOutcome outcome = service_.useConsumable(recordId, selectedMedicineItemId_);
        if (outcome == pokemon::UseConsumableOutcome::NotApplicable) {
          showMessage(tr(STR_POKEMON_NOT_APPLICABLE), bagScreen);
        } else if (outcome != pokemon::UseConsumableOutcome::Applied) {
          showMessage(tr(STR_POKEMON_SAVE_ERROR), bagScreen);
        } else if (service_.consumeBagItem(selectedMedicineItemId_) != pokemon::ServiceStatus::Ok) {
          showMessage(tr(STR_POKEMON_SAVE_ERROR), bagScreen);
        } else if (refreshSnapshot()) {
          setScreen(Screen::Party);
        }
        return;
      }
      const auto status = service_.useEvolutionItem(recordId, selectedItem_);
      if (status == pokemon::ServiceStatus::NotApplicable)
        showMessage(tr(STR_POKEMON_NOT_APPLICABLE), bagScreen);
      else if (status != pokemon::ServiceStatus::Ok)
        showMessage(tr(STR_POKEMON_SAVE_ERROR), bagScreen);
      else {
        if (!refreshSnapshot()) return;
        setScreen(Screen::Party);
      }
      return;
    }
    case Screen::TmReplaceSlot: {
      if (selected_ >= static_cast<int>(pokemon::BATTLE_MOVE_SLOTS)) {
        setScreen(Screen::BagMachine);
        return;
      }
      const pokemon::TeachMoveOutcome outcome = service_.teachMove(focusedRecordId_, selectedMachineMoveId_, selected_);
      if (outcome != pokemon::TeachMoveOutcome::Learned) {
        showMessage(tr(STR_POKEMON_SAVE_ERROR), Screen::BagMachine);
        return;
      }
      if (service_.consumeBagItem(selectedMachineItemId_) != pokemon::ServiceStatus::Ok) {
        showMessage(tr(STR_POKEMON_SAVE_ERROR), Screen::BagMachine);
        return;
      }
      if (!refreshSnapshot()) return;
      setScreen(Screen::Party);
      return;
    }
    case Screen::Pokedex:
      pokedexSpecies_ = static_cast<uint16_t>(selected_ + 1);
      if (pokemon::isSpeciesMarked(snapshot_.state.seenSpecies, pokedexSpecies_)) {
        setScreen(Screen::PokedexDetail);
      }
      return;
    case Screen::PokedexDetail:
      return;
    case Screen::Event: {
      const pokemon::PendingEvent* active = pokemon::pendingEventFront(snapshot_.state);
      if (active == nullptr) {
        setScreen(Screen::Menu);
        return;
      }
      const pokemon::PendingEvent pending = *active;
      if (pending.kind == pokemon::PendingEventKind::Encounter) {
        if (selected_ == 0) {
          // "Catch" now opens a battle instead of resolving instantly - the
          // wild Pokemon must be fought and/or thrown a ball at.
          if (!enterBattle(pending)) showMessage(tr(STR_POKEMON_SAVE_ERROR), Screen::Event);
          return;
        }
        uint32_t caught = 0;
        if (service_.resolveEncounter(pokemon::EncounterChoice::Pass, caught) != pokemon::ServiceStatus::Ok) {
          showMessage(tr(STR_POKEMON_SAVE_ERROR), Screen::Event);
          return;
        }
        if (!refreshSnapshot()) return;
        setScreen(pokemon::pendingEventFront(snapshot_.state) == nullptr ? Screen::Menu : Screen::Event);
      } else if (pending.kind == pokemon::PendingEventKind::Item) {
        if (service_.acknowledgeItem() != pokemon::ServiceStatus::Ok)
          showMessage(tr(STR_POKEMON_SAVE_ERROR), Screen::Event);
        else {
          if (!refreshSnapshot()) return;
          setScreen(pokemon::pendingEventFront(snapshot_.state) == nullptr ? Screen::Menu : Screen::Event);
        }
      } else if (pending.kind == pokemon::PendingEventKind::Evolution) {
        const auto choice = selected_ == 0 ? pokemon::EvolutionChoice::Evolve : pokemon::EvolutionChoice::Cancel;
        if (service_.resolveEvolution(choice) != pokemon::ServiceStatus::Ok)
          showMessage(tr(STR_POKEMON_SAVE_ERROR), Screen::Event);
        else {
          if (!refreshSnapshot()) return;
          setScreen(pokemon::pendingEventFront(snapshot_.state) == nullptr ? Screen::Menu : Screen::Event);
        }
      } else if (pending.kind == pokemon::PendingEventKind::MoveLearn) {
        // selected_ < BATTLE_MOVE_SLOTS picks which existing move to
        // overwrite; the last row (index BATTLE_MOVE_SLOTS) skips learning
        // it. Either way the pending event is dequeued.
        const int replaceSlot = selected_ < static_cast<int>(pokemon::BATTLE_MOVE_SLOTS) ? selected_ : -1;
        if (service_.resolveMoveLearn(replaceSlot) != pokemon::ServiceStatus::Ok)
          showMessage(tr(STR_POKEMON_SAVE_ERROR), Screen::Event);
        else {
          if (!refreshSnapshot()) return;
          setScreen(pokemon::pendingEventFront(snapshot_.state) == nullptr ? Screen::Menu : Screen::Event);
        }
      }
      return;
    }
    case Screen::Battle:
      if (selected_ == 0) {
        if (battlePlayerMoveCount() == 0) {
          showMessage(tr(STR_POKEMON_NOT_APPLICABLE), Screen::Battle);
          return;
        }
        setScreen(Screen::BattleMoves);
      } else if (gymChallengeIndex_ == 0 && selected_ == 1) {
        // Trainer battles (gym/Elite Four) have no BALL option - their
        // logicalCount() is 2, so selected_ == 1 there is already RUN.
        setScreen(Screen::BattleBalls);
      } else {
        resolveBattleAsPass();
      }
      return;
    case Screen::BattleMoves: {
      if (selected_ < 0 || selected_ >= battlePlayerMoveCount()) return;
      if (battlePlayer_.moves[selected_].currentPp == 0) {
        showMessage(tr(STR_POKEMON_NOT_APPLICABLE), Screen::BattleMoves);
        return;
      }
      const pokemon::BattleTurnResult result =
          service_.resolveBattleTurn(battlePlayer_, battleOpponent_, static_cast<uint8_t>(selected_));
      savePlayerBattleEntry();
      buildBattleLog(result);
      if (result.outcome == pokemon::BattleOutcome::PlayerWon) {
        finishBattleAfterWildFainted();
      } else if (result.outcome == pokemon::BattleOutcome::OpponentWon) {
        finishBattleAfterPlayerFainted();
      } else {
        setScreen(Screen::Battle);
      }
      return;
    }
    case Screen::BattleBalls: {
      if (selected_ < 0 || selected_ >= 4) return;
      const auto ballItemIndex = static_cast<size_t>(selected_);
      if (snapshot_.state.bagCounts[ballItemIndex] == 0) {
        showMessage(tr(STR_POKEMON_NOT_APPLICABLE), Screen::BattleBalls);
        return;
      }
      const auto ball = static_cast<pokemon::BallKind>(selected_);
      const bool caught = service_.attemptBattleCatch(battleOpponent_, ball);
      const auto itemId = static_cast<uint8_t>(pokemon::EVOLUTION_ITEM_COUNT + 1U + ballItemIndex);
      if (service_.consumeBagItem(itemId) != pokemon::ServiceStatus::Ok) {
        showMessage(tr(STR_POKEMON_SAVE_ERROR), Screen::Battle);
        return;
      }
      savePlayerBattleEntry();
      if (!refreshSnapshot()) return;
      if (!caught) {
        char line[96];
        snprintf(line, sizeof(line), tr(STR_POKEMON_BROKE_FREE), speciesName(battleOpponent_.speciesId));
        snprintf(battleLog_, sizeof(battleLog_), "%s", line);
        setScreen(Screen::Battle);
        return;
      }
      const uint16_t caughtSpecies = battleOpponent_.speciesId;
      uint32_t caughtRecordId = 0;
      if (service_.resolveEncounter(pokemon::EncounterChoice::Catch, caughtRecordId) != pokemon::ServiceStatus::Ok ||
          caughtRecordId == 0) {
        showMessage(tr(STR_POKEMON_SAVE_ERROR), Screen::Menu);
        return;
      }
      if (!refreshSnapshot()) return;
      nicknamePrompt_ = pokemon::PokemonPromptContext::forCaught(caughtSpecies, caughtRecordId);
      snprintf(message_, sizeof(message_), tr(STR_POKEMON_NICKNAME_QUESTION), speciesName(caughtSpecies));
      setScreen(Screen::NicknameQuestion);
      return;
    }
    case Screen::GymList: {
      const auto gymIndex = static_cast<uint8_t>(selected_ + 1);
      if (pokemon::gymProgressFor(snapshot_.state.battleProgress, gymIndex) != pokemon::GymProgress::Available) {
        showMessage(tr(STR_POKEMON_NOT_APPLICABLE), Screen::GymList);
        return;
      }
      if (!enterGymBattle(gymIndex)) showMessage(tr(STR_POKEMON_SAVE_ERROR), Screen::GymList);
      return;
    }
    case Screen::Badges:
      return;  // display-only, nothing to activate
    case Screen::ResetFirst:
      if (selected_ == 0)
        setScreen(Screen::ResetFinal);
      else
        setScreen(Screen::Menu);
      return;
    case Screen::ResetFinal:
      if (selected_ == 0 && service_.reset() == pokemon::ServiceStatus::Ok) {
        snapshot_ = {};
        setScreen(Screen::Starter);
      } else if (selected_ == 0)
        showMessage(tr(STR_POKEMON_SAVE_ERROR), Screen::Menu);
      else
        setScreen(Screen::Menu);
      return;
    case Screen::Message:
      setScreen(returnScreen_);
      return;
  }
}

void PokemonActivity::goBack() {
  switch (screen_) {
    case Screen::Starter:
    case Screen::Menu:
      finishAfterBackPress();
      return;
    case Screen::Gender:
      setScreen(Screen::Starter);
      return;
    case Screen::NicknameQuestion:
      if (nicknamePrompt_.isStarter())
        setScreen(Screen::Gender);
      else {
        nicknamePrompt_ = {};
        setScreen(pokemon::pendingEventFront(snapshot_.state) == nullptr ? Screen::Menu : Screen::Event);
      }
      return;
    case Screen::Summary:
      setScreen(Screen::Actions);
      return;
    case Screen::Actions:
      setScreen(actionSource_);
      return;
    case Screen::Move:
      setScreen(Screen::Party);
      return;
    case Screen::Moveset:
      setScreen(Screen::Actions);
      return;
    case Screen::MovesetPick:
      setScreen(Screen::Moveset);
      return;
    case Screen::TmReplaceSlot:
      setScreen(Screen::BagMachine);
      return;
    case Screen::PcOrder:
      setScreen(Screen::Pc);
      return;
    case Screen::ItemTarget:
      setScreen(bagCategory_ == BagCategory::Evolution  ? Screen::BagEvolution
                : bagCategory_ == BagCategory::Medicine ? Screen::BagMedicine
                                                        : Screen::BagMachine);
      return;
    case Screen::BagEvolution:
    case Screen::BagMedicine:
    case Screen::BagMachine:
      setScreen(Screen::Bag);
      return;
    case Screen::Battle:
      // Back out of a battle the same way RUN does - never leaves the
      // player stuck mid-fight with no way to exit via hardware Back.
      resolveBattleAsPass();
      return;
    case Screen::BattleMoves:
    case Screen::BattleBalls:
      setScreen(Screen::Battle);
      return;
    case Screen::PokedexDetail:
      setScreen(Screen::Pokedex, static_cast<int>(pokedexSpecies_ - 1U));
      return;
    case Screen::Message:
      setScreen(returnScreen_);
      return;
    default:
      setScreen(Screen::Menu);
      return;
  }
}

void PokemonActivity::loop() {
  if (TouchHeaderBackButton::wasTapped(mappedInput, renderer) ||
      mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    goBack();
    return;
  }
  if (uiReady_) {
    const auto snap = touchSnapshotFrom(mappedInput);
    if (snap.touchPressed || snap.touchReleased) {
      const auto event = app_.route(snap);
      if (app_.invalidated()) requestUpdate();
      if (event) return;
    }
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activate();
    return;
  }
  const int count = logicalCount();
  if (count <= 0) return;
  const int perPage = rowsPerPage();
  const auto move = [this, count](const int next) {
    selected_ = next;
    requestUpdate();
  };
  // The side buttons (Up/Down) jump a full page at a time; the front
  // buttons (Left/Right) step one row - fewer presses to get through long
  // lists (Bag, Pokedex, PC box...), easier on the buttons. nextPageIndex/
  // previousPageIndex already fall back to a single step when everything
  // fits on one page, so this is safe on short lists too.
  navigator_.onPressAndContinuous({MappedInputManager::Button::Right},
                                  [this, count, &move] { move(ButtonNavigator::nextIndex(selected_, count)); });
  navigator_.onPressAndContinuous({MappedInputManager::Button::Left},
                                  [this, count, &move] { move(ButtonNavigator::previousIndex(selected_, count)); });
  navigator_.onPressAndContinuous({MappedInputManager::Button::Down}, [this, count, perPage, &move] {
    move(ButtonNavigator::nextPageIndex(selected_, count, perPage));
  });
  navigator_.onPressAndContinuous({MappedInputManager::Button::Up}, [this, count, perPage, &move] {
    move(ButtonNavigator::previousPageIndex(selected_, count, perPage));
  });
}

void PokemonActivity::screenBuilder(UiApp::ScreenType& screen, void* user) {
  static_cast<PokemonActivity*>(user)->buildUi(screen);
}

void PokemonActivity::onRow(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<PokemonActivity*>(user);
  if (event.value < 0 || event.value >= self->logicalCount()) return;
  self->selected_ = event.value;
  self->app_.clearTapFlash();
  self->activate();
}

void PokemonActivity::buildRows() {
  for (size_t i = 0; i < rows_.size(); ++i) {
    rows_[i] = {};
    labels_[i].fill('\0');
    values_[i].fill('\0');
  }
  const int start = pageStart();
  const int total = logicalCount();
  rowCount_ = std::min(rowsPerPage(), std::max(0, total - start));
  const auto row = [this, start](const int local, const char* label, const char* value = nullptr) {
    snprintf(labels_[local].data(), labels_[local].size(), "%s", label == nullptr ? "" : label);
    if (value != nullptr) snprintf(values_[local].data(), values_[local].size(), "%s", value);
    rows_[local].label = labels_[local].data();
    rows_[local].value = value == nullptr ? nullptr : values_[local].data();
    rows_[local].actionValue = static_cast<int16_t>(start + local);
  };

  for (int local = 0; local < rowCount_; ++local) {
    const int index = start + local;
    switch (screen_) {
      case Screen::Starter:
        row(local, speciesName(STARTERS[index]));
        break;
      case Screen::Gender:
        row(local, index == 0 ? tr(STR_POKEMON_MALE) : tr(STR_POKEMON_FEMALE));
        break;
      case Screen::NicknameQuestion:
      case Screen::ResetFirst:
      case Screen::ResetFinal:
        row(local, index == 0 ? tr(STR_YES) : tr(STR_NO));
        break;
      case Screen::Menu:
        if (index == 7) {
          row(local, tr(STR_POKEMON_HOME_SCREEN),
              SETTINGS.pokemonHomeScreen != 0 ? tr(STR_POKEMON_ON) : tr(STR_POKEMON_OFF));
        } else {
          row(local, index == 0   ? tr(STR_POKEMON_PARTY)
                     : index == 1 ? tr(STR_POKEDEX)
                     : index == 2 ? tr(STR_POKEMON_PC_BOX)
                     : index == 3 ? tr(STR_POKEMON_PC_SORT)
                     : index == 4 ? tr(STR_POKEMON_BAG)
                     : index == 5 ? tr(STR_POKEMON_GYM_BATTLE)
                     : index == 6 ? tr(STR_POKEMON_BADGES)
                                  : tr(STR_POKEMON_RESET));
        }
        break;
      case Screen::Party:
      case Screen::Move:
      case Screen::ItemTarget: {
        if (index >= snapshot_.partyCount) {
          row(local, tr(STR_POKEMON_EMPTY));
          break;
        }
        const auto& record = snapshot_.party[index];
        char value[24];
        snprintf(value, sizeof(value), "%s %u  %s", tr(STR_POKEMON_LEVEL), pokemon::levelForXp(record.totalXp),
                 genderText(record.gender));
        row(local, record.nickname[0] == '\0' ? speciesName(record.speciesId) : record.nickname.data(), value);
        break;
      }
      case Screen::Actions: {
        const auto actions = pokemon::collectionActions(actionSource_ == Screen::Party, snapshot_.partyCount);
        if (index >= actions.count) break;
        const char* label = nullptr;
        switch (actions.items[index]) {
          case pokemon::CollectionAction::Summary:
            label = tr(STR_POKEMON_SUMMARY);
            break;
          case pokemon::CollectionAction::Moveset:
            label = tr(STR_POKEMON_MOVES);
            break;
          case pokemon::CollectionAction::Move:
            label = tr(STR_POKEMON_MOVE);
            break;
          case pokemon::CollectionAction::Deposit:
            label = tr(STR_POKEMON_DEPOSIT);
            break;
          case pokemon::CollectionAction::Withdraw:
            label = tr(STR_POKEMON_WITHDRAW);
            break;
          case pokemon::CollectionAction::Rename:
            label = tr(STR_POKEMON_RENAME);
            break;
          case pokemon::CollectionAction::EvolutionPrompts:
            label = tr(STR_POKEMON_EVOLUTIONS);
            break;
        }
        row(local, label);
        break;
      }
      case Screen::Moveset: {
        pokemon::PokemonRecord record{};
        const pokemon::BattleRecordEntry entry =
            service_.readRecord(focusedRecordId_, record) == pokemon::ServiceStatus::Ok
                ? service_.peekBattleMoves(record)
                : pokemon::BattleRecordEntry{};
        const pokemon::MoveData* move = pokemon::moveData(entry.moves[index]);
        char value[16];
        snprintf(value, sizeof(value), "PP %u/%u", entry.pp[index], move == nullptr ? 0 : move->pp);
        row(local, move == nullptr ? "-" : move->name, value);
        break;
      }
      case Screen::MovesetPick: {
        pokemon::PokemonRecord record{};
        if (service_.readRecord(focusedRecordId_, record) != pokemon::ServiceStatus::Ok) break;
        const pokemon::BattleRecordEntry entry = service_.peekBattleMoves(record);
        const size_t learnable = learnableMoveCount(record.speciesId, pokemon::levelForXp(record.totalXp), entry);
        if (static_cast<size_t>(index) >= learnable) {
          row(local, tr(STR_POKEMON_FORGET));
          break;
        }
        const uint8_t moveId =
            learnableMoveIdAt(record.speciesId, pokemon::levelForXp(record.totalXp), entry, static_cast<size_t>(index));
        const pokemon::MoveData* move = pokemon::moveData(moveId);
        row(local, move == nullptr ? "?" : move->name);
        break;
      }
      case Screen::TmReplaceSlot: {
        if (index >= static_cast<int>(pokemon::BATTLE_MOVE_SLOTS)) {
          row(local, tr(STR_POKEMON_CANCEL));
          break;
        }
        pokemon::PokemonRecord record{};
        const pokemon::BattleRecordEntry entry =
            service_.readRecord(focusedRecordId_, record) == pokemon::ServiceStatus::Ok
                ? service_.peekBattleMoves(record)
                : pokemon::BattleRecordEntry{};
        const pokemon::MoveData* move = pokemon::moveData(entry.moves[index]);
        char value[16];
        snprintf(value, sizeof(value), "PP %u/%u", entry.pp[index], move == nullptr ? 0 : move->pp);
        row(local, move == nullptr ? "-" : move->name, value);
        break;
      }
      case Screen::Pc: {
        if (local == 0) {
          pcCount_ = 0;
          if (service_.readPcPage(pcOrder_, start, pcPage_, pcCount_) != pokemon::ServiceStatus::Ok) {
            rowCount_ = 1;
            row(local, tr(STR_POKEMON_LOAD_ERROR));
            break;
          }
          rowCount_ = static_cast<int>(pcCount_);
          if (rowCount_ == 0) break;
        }
        if (local >= static_cast<int>(pcCount_)) break;
        const auto& record = pcPage_[local];
        char value[24];
        snprintf(value, sizeof(value), "%s %u  %s", tr(STR_POKEMON_LEVEL), pokemon::levelForXp(record.totalXp),
                 genderText(record.gender));
        row(local, record.nickname[0] == '\0' ? speciesName(record.speciesId) : record.nickname.data(), value);
        break;
      }
      case Screen::PcOrder:
        row(local, index == 0   ? tr(STR_POKEMON_CATCH_DATE)
                   : index == 1 ? tr(STR_POKEMON_NUMBER)
                                : tr(STR_POKEMON_ALPHABETICAL));
        break;
      case Screen::Bag:
        row(local, index == 0   ? tr(STR_POKEMON_BAG_EVOLUTION)
                   : index == 1 ? tr(STR_POKEMON_BAG_MEDICINE)
                                : tr(STR_POKEMON_BAG_MACHINES));
        break;
      case Screen::BagEvolution: {
        const auto item = static_cast<pokemon::EvolutionItem>(index + 1);
        char count[16];
        snprintf(count, sizeof(count), "× %u", snapshot_.state.itemCounts[index]);
        row(local, itemName(item), count);
        break;
      }
      case Screen::BagMedicine:
      case Screen::BagMachine: {
        const bool machine = screen_ == Screen::BagMachine;
        const uint8_t itemId =
            bagItemIdAt(static_cast<size_t>(index), machine ? isMachineCategory : isMedicineCategory);
        const auto bagIndex = static_cast<size_t>(itemId - pokemon::EVOLUTION_ITEM_COUNT - 1U);
        const pokemon::ItemData* data = pokemon::itemData(itemId);
        char count[16];
        snprintf(count, sizeof(count), "× %u",
                 bagIndex < snapshot_.state.bagCounts.size() ? snapshot_.state.bagCounts[bagIndex] : 0);
        char label[56];
        if (machine && data != nullptr) {
          // TM/HM names alone ("TM01") don't say what they teach - show the
          // move name too so browsing the list doesn't require a lookup.
          const pokemon::MoveData* move = pokemon::moveData(data->teachesMoveId);
          snprintf(label, sizeof(label), "%s - %s", data->name, move == nullptr ? "?" : move->name);
        } else {
          snprintf(label, sizeof(label), "%s", data == nullptr ? "?" : data->name);
        }
        row(local, label, count);
        break;
      }
      case Screen::Pokedex: {
        const uint16_t speciesId = static_cast<uint16_t>(index + 1);
        const bool caught = pokemon::isSpeciesMarked(snapshot_.state.caughtSpecies, speciesId);
        const bool seen = pokemon::isSpeciesMarked(snapshot_.state.seenSpecies, speciesId);
        char label[40];
        snprintf(label, sizeof(label), "No. %03u  %s", speciesId, seen ? speciesName(speciesId) : "???");
        row(local, label, caught ? tr(STR_POKEMON_CAUGHT) : seen ? tr(STR_POKEMON_SEEN) : nullptr);
        break;
      }
      case Screen::Event:
        if (const pokemon::PendingEvent* pending = pokemon::pendingEventFront(snapshot_.state);
            pending != nullptr && pending->kind == pokemon::PendingEventKind::Encounter) {
          row(local, index == 0 ? tr(STR_POKEMON_CATCH) : tr(STR_POKEMON_PASS));
        } else if (pending != nullptr && pending->kind == pokemon::PendingEventKind::Evolution) {
          row(local, index == 0 ? tr(STR_POKEMON_EVOLVE) : tr(STR_POKEMON_CANCEL));
        } else if (pending != nullptr && pending->kind == pokemon::PendingEventKind::MoveLearn) {
          if (index >= static_cast<int>(pokemon::BATTLE_MOVE_SLOTS)) {
            row(local, tr(STR_POKEMON_CANCEL));
            break;
          }
          pokemon::PokemonRecord record{};
          const pokemon::BattleRecordEntry entry =
              service_.readRecord(pending->recordId, record) == pokemon::ServiceStatus::Ok
                  ? service_.peekBattleMoves(record)
                  : pokemon::BattleRecordEntry{};
          const pokemon::MoveData* move = pokemon::moveData(entry.moves[index]);
          char value[16];
          snprintf(value, sizeof(value), "PP %u/%u", entry.pp[index], move == nullptr ? 0 : move->pp);
          row(local, move == nullptr ? "-" : move->name, value);
        } else
          row(local, tr(STR_OK));
        break;
      case Screen::Battle:
        row(local, index == 0                                ? tr(STR_POKEMON_FIGHT)
                   : (gymChallengeIndex_ == 0 && index == 1) ? tr(STR_POKEMON_BALL)
                                                             : tr(STR_POKEMON_RUN));
        break;
      case Screen::BattleMoves: {
        const uint8_t moveId = battlePlayer_.moves[index].moveId;
        const pokemon::MoveData* move = pokemon::moveData(moveId);
        char value[16];
        snprintf(value, sizeof(value), "PP %u/%u", battlePlayer_.moves[index].currentPp,
                 move == nullptr ? 0 : move->pp);
        row(local, move == nullptr ? "?" : move->name, value);
        break;
      }
      case Screen::BattleBalls: {
        const pokemon::ItemData* item =
            pokemon::itemData(static_cast<uint8_t>(pokemon::EVOLUTION_ITEM_COUNT + 1 + index));
        char value[16];
        snprintf(value, sizeof(value), "× %u", snapshot_.state.bagCounts[index]);
        row(local, item == nullptr ? "?" : item->name, value);
        break;
      }
      case Screen::GymList: {
        const auto gymIndex = static_cast<uint8_t>(index + 1);
        const pokemon::GymData* gym = pokemon::gymData(gymIndex);
        char label[40];
        if (gymIndex <= 8U) {
          snprintf(label, sizeof(label), "%s", gym == nullptr ? "?" : gym->leaderName);
        } else {
          snprintf(label, sizeof(label), "%s - %s", tr(STR_POKEMON_ELITE_FOUR), gym == nullptr ? "?" : gym->leaderName);
        }
        const pokemon::GymProgress progress = pokemon::gymProgressFor(snapshot_.state.battleProgress, gymIndex);
        const char* value = progress == pokemon::GymProgress::Defeated ? tr(STR_POKEMON_GYM_DEFEATED)
                            : progress == pokemon::GymProgress::Locked ? tr(STR_POKEMON_GYM_LOCKED)
                                                                       : nullptr;
        row(local, label, value);
        break;
      }
      case Screen::Badges: {
        const auto gymIndex = static_cast<uint8_t>(index + 1);
        const pokemon::GymData* gym = pokemon::gymData(gymIndex);
        const bool earned =
            pokemon::gymProgressFor(snapshot_.state.battleProgress, gymIndex) == pokemon::GymProgress::Defeated;
        row(local, gym == nullptr ? "?" : gym->badgeName,
            earned ? tr(STR_POKEMON_GYM_DEFEATED) : tr(STR_POKEMON_GYM_LOCKED));
        break;
      }
      case Screen::Summary:
      case Screen::PokedexDetail:
      case Screen::Message:
        break;
    }
  }
}

void PokemonActivity::buildList(UiApp::ScreenType& screen) {
  buildRows();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const bool artRows = screen_ == Screen::Starter || screen_ == Screen::Party || screen_ == Screen::Move ||
                       screen_ == Screen::Pc || screen_ == Screen::BagEvolution || screen_ == Screen::ItemTarget ||
                       screen_ == Screen::Pokedex;
  int top = listTop();
  rowHeight_ = 64;
  const bool bottomAnchored = screen_ == Screen::Event || screen_ == Screen::Battle || screen_ == Screen::BattleMoves ||
                              screen_ == Screen::BattleBalls;
  if (bottomAnchored) top = renderer.getScreenHeight() - metrics.buttonHintsHeight - rowCount_ * rowHeight_ - 8;
  listBounds_ = Rect{8, top, renderer.getScreenWidth() - 16, rowCount_ * rowHeight_};
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(listBounds_.y), 8,
                  static_cast<int16_t>(renderer.getScreenHeight() - listBounds_.y - listBounds_.height), 8});
  fui::ListProps props;
  props.items = rows_.data();
  props.count = static_cast<uint16_t>(std::max(0, rowCount_));
  props.selectedIndex = static_cast<int16_t>(selected_ - pageStart());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.rowHeight = static_cast<int16_t>(rowHeight_);
  props.rowGap = 0;
  // A Game Boy-style cursor keeps every choice on white paper. Grey dither
  // ghosts under FAST e-ink refreshes, while an inverted row hides SD-backed
  // black artwork. Only the spacing differs between text and artwork rows.
  const pokemon::PokemonListPresentation presentation = pokemon::pokemonListPresentation(artRows);
  props.sidePadding = static_cast<int16_t>(presentation.sidePadding);
  props.rowStyles = presentation.rowStyles;
  props.selectionMarker = fui::SelectionMarker::Triangle;
  props.markerInset = static_cast<int16_t>(presentation.markerInset);
  props.valueInset = 8;
  screen.list(props);
}

void PokemonActivity::buildUi(UiApp::ScreenType& screen) {
  if (isListScreen()) buildList(screen);
}

void PokemonActivity::renderFocused() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (screen_ == Screen::PokedexDetail) {
    int marginTop = 0, marginRight = 0, marginBottom = 0, marginLeft = 0;
    renderer.getOrientedViewableTRBL(&marginTop, &marginRight, &marginBottom, &marginLeft);
    const bool landscape = renderer.getScreenWidth() > renderer.getScreenHeight();
    const auto card = pokemon::pokemonPokedexCardBounds(renderer.getScreenWidth() - marginLeft - marginRight,
                                                        renderer.getScreenHeight(), marginTop, marginBottom,
                                                        metrics.buttonHintsHeight, landscape);
    const Rect cardBounds{marginLeft + card.x, card.y, card.width, card.height};
    const bool rendered = pokemon::drawPokemonPokedexArt(renderer, pokedexSpecies_, landscape, cardBounds, false);
    if (!rendered) {
      pokemon::drawPokemonSpeciesArt(renderer, pokedexSpecies_, true,
                                     Rect{(renderer.getScreenWidth() - 120) / 2, marginTop + 80, 120, 90});
      centered(renderer, UI_12_FONT_ID, marginTop + 190, tr(STR_POKEMON_LOAD_ERROR), EpdFontFamily::BOLD);
    }
#if defined(SIMULATOR)
    else {
      LOG_INF("SMOKE", "Pokemon Pokedex detail card rendered");
    }
#endif
    return;
  }
  const int contentTop = metrics.topPadding + TouchHeaderBackButton::height(metrics, mappedInput) + 14;
  if (screen_ == Screen::Message) {
    centered(renderer, UI_12_FONT_ID, contentTop + 100, message_, EpdFontFamily::BOLD);
    return;
  }
  if (screen_ == Screen::Starter) {
    centered(renderer, UI_12_FONT_ID, contentTop + 18, tr(STR_POKEMON_CHOOSE_PARTNER), EpdFontFamily::BOLD);
    return;
  }
  if (screen_ == Screen::Gender) {
    centered(renderer, UI_12_FONT_ID, contentTop + 18, tr(STR_POKEMON_CHOOSE_GENDER), EpdFontFamily::BOLD);
    pokemon::drawPokemonSpeciesArt(renderer, starterSpecies_, true,
                                   Rect{(renderer.getScreenWidth() - 120) / 2, contentTop + 54, 120, 90});
    return;
  }
  if (screen_ == Screen::Pc && logicalCount() == 0) {
    centered(renderer, UI_12_FONT_ID, contentTop + 100, tr(STR_POKEMON_NO_STORED), EpdFontFamily::BOLD);
    return;
  }
  if (screen_ == Screen::NicknameQuestion || screen_ == Screen::ResetFirst || screen_ == Screen::ResetFinal) {
    const char* prompt = screen_ == Screen::NicknameQuestion ? message_
                         : screen_ == Screen::ResetFirst     ? tr(STR_POKEMON_RESET_QUESTION)
                                                             : tr(STR_POKEMON_RESET_CONFIRM);
    centered(renderer, UI_12_FONT_ID, contentTop + 18, prompt, EpdFontFamily::BOLD);
    if (screen_ == Screen::NicknameQuestion) {
      pokemon::drawPokemonSpeciesArt(renderer, nicknamePrompt_.speciesId, true,
                                     Rect{(renderer.getScreenWidth() - 120) / 2, contentTop + 82, 120, 90});
    }
    return;
  }
  if (screen_ == Screen::Summary) {
    pokemon::PokemonRecord record{};
    if (service_.readRecord(focusedRecordId_, record) != pokemon::ServiceStatus::Ok) {
      LOG_ERR("PokemonActivity", "Failed to load summary record %lu", static_cast<unsigned long>(focusedRecordId_));
      centered(renderer, UI_12_FONT_ID, contentTop + 100, tr(STR_POKEMON_LOAD_ERROR), EpdFontFamily::BOLD);
      return;
    }
    const pokemon::SpeciesData* species = pokemon::speciesData(record.speciesId);
    if (species == nullptr) {
      LOG_ERR("PokemonActivity", "Summary record %lu has invalid species %u",
              static_cast<unsigned long>(focusedRecordId_), record.speciesId);
      centered(renderer, UI_12_FONT_ID, contentTop + 100, tr(STR_POKEMON_LOAD_ERROR), EpdFontFamily::BOLD);
      return;
    }
    const bool landscape = renderer.getScreenWidth() > renderer.getScreenHeight();
    const int artX = landscape ? 28 : (renderer.getScreenWidth() - 120) / 2;
    const int artY = contentTop + 8;
    pokemon::drawPokemonSpeciesArt(renderer, record.speciesId, true, Rect{artX, artY, 120, 90});
    const int textX = landscape ? 178 : 28;
    int y = landscape ? contentTop + 8 : artY + 106;
    renderer.drawText(UI_12_FONT_ID, textX, y, speciesName(record.speciesId), true, EpdFontFamily::BOLD);
    y += 25;
    renderer.drawText(UI_10_FONT_ID, textX, y, record.nickname[0] == '\0' ? "" : record.nickname.data());
    y += 27;
    char line[96];
    snprintf(line, sizeof(line), "%s %03u    %s %u    %s", tr(STR_POKEMON_NUMBER_SHORT), record.speciesId,
             tr(STR_POKEMON_LEVEL), pokemon::levelForXp(record.totalXp), genderText(record.gender));
    renderer.drawText(UI_10_FONT_ID, textX, y, line);
    y += 26;
    const int valueRight = renderer.getScreenWidth() - 28;
    const auto drawField = [this, textX, valueRight](const int fieldY, const char* label, const char* value) {
      renderer.drawText(UI_10_FONT_ID, textX, fieldY, label, true, EpdFontFamily::BOLD);
      const int width = renderer.getTextWidth(UI_10_FONT_ID, value);
      renderer.drawText(UI_10_FONT_ID, pokemon::pokemonRightAlignedX(valueRight, width), fieldY, value);
    };
    char types[48]{};
    if (species->secondaryType == pokemon::PokemonType::None) {
      snprintf(types, sizeof(types), "%s", typeName(species->primaryType));
    } else {
      snprintf(types, sizeof(types), "%s / %s", typeName(species->primaryType), typeName(species->secondaryType));
    }
    drawField(y, tr(STR_POKEMON_TYPE), types);
    y += 26;
    const pokemon::LevelXpProgress progress = pokemon::levelXpProgress(record.totalXp);
    if (progress.required == 0) {
      snprintf(line, sizeof(line), "%s", tr(STR_POKEMON_MAX));
    } else {
      snprintf(line, sizeof(line), "%lu / %lu", static_cast<unsigned long>(progress.earned),
               static_cast<unsigned long>(progress.required));
    }
    drawField(y, tr(STR_POKEMON_EXP_POINTS), line);
    y += 26;
    snprintf(line, sizeof(line), "%s %u", tr(STR_POKEMON_LEVEL), record.caughtLevel);
    drawField(y, tr(STR_POKEMON_MET), line);
    y += 26;
    const auto evolutions = pokemon::evolutionsFor(record.speciesId);
    if (evolutions.empty()) {
      drawField(y, tr(STR_POKEMON_EVOLUTION), tr(STR_POKEMON_NO_EVOLUTION));
      y += 26;
    } else {
      bool first = true;
      for (const pokemon::EvolutionRule& rule : evolutions) {
        if (rule.trigger == pokemon::EvolutionTrigger::Level) {
          snprintf(line, sizeof(line), tr(STR_POKEMON_EVOLVES_LEVEL), rule.minimumLevel,
                   speciesName(rule.targetSpeciesId));
        } else {
          snprintf(line, sizeof(line), tr(STR_POKEMON_EVOLVES_ITEM), itemName(rule.item),
                   speciesName(rule.targetSpeciesId));
        }
        if (first) {
          renderer.drawText(UI_10_FONT_ID, textX, y, tr(STR_POKEMON_EVOLUTION), true, EpdFontFamily::BOLD);
          first = false;
        }
        renderer.drawText(UI_10_FONT_ID,
                          pokemon::pokemonRightAlignedX(valueRight, renderer.getTextWidth(UI_10_FONT_ID, line)), y,
                          line);
        y += 26;
      }
    }
    const bool prompts = (record.flags & pokemon::recordFlag(pokemon::RecordFlag::EvolutionPromptsDisabled)) == 0;
    drawField(y, tr(STR_POKEMON_EVOLUTION_PROMPTS_FIELD), prompts ? tr(STR_POKEMON_ON) : tr(STR_POKEMON_OFF));
    y += 26;
    // 2 moves per line (not 1) - landscape's vertical budget is tight
    // enough already (see the roadmap's GĐ 7 note) that a 4-line block
    // here would run past the button hints.
    const pokemon::BattleRecordEntry moves = service_.peekBattleMoves(record);
    const int moveColumnX = textX + (valueRight - textX) / 2 + 10;
    for (size_t slot = 0; slot < pokemon::BATTLE_MOVE_SLOTS; slot += 2) {
      char left[40] = "-";
      char right[40] = "-";
      if (moves.moves[slot] != 0) {
        const pokemon::MoveData* move = pokemon::moveData(moves.moves[slot]);
        snprintf(left, sizeof(left), "%s %u/%u", move == nullptr ? "?" : move->name, moves.pp[slot],
                 move == nullptr ? 0 : move->pp);
      }
      renderer.drawText(UI_10_FONT_ID, textX, y, left);
      if (moves.moves[slot + 1] != 0) {
        const pokemon::MoveData* move = pokemon::moveData(moves.moves[slot + 1]);
        snprintf(right, sizeof(right), "%s %u/%u", move == nullptr ? "?" : move->name, moves.pp[slot + 1],
                 move == nullptr ? 0 : move->pp);
        renderer.drawText(UI_10_FONT_ID, moveColumnX, y, right);
      }
      y += 26;
    }
    return;
  }
  if (screen_ == Screen::Battle || screen_ == Screen::BattleMoves || screen_ == Screen::BattleBalls) {
    renderBattleHud();
    return;
  }
  if (screen_ != Screen::Event) return;
  const pokemon::PendingEvent* active = pokemon::pendingEventFront(snapshot_.state);
  if (active == nullptr) return;
  const pokemon::PendingEvent& pending = *active;
  char line[96];
  if (pending.kind == pokemon::PendingEventKind::Encounter) {
    pokemon::drawPokemonSpeciesArt(renderer, pending.speciesId, true,
                                   Rect{(renderer.getScreenWidth() - 120) / 2, contentTop + 8, 120, 90});
    snprintf(line, sizeof(line), tr(STR_POKEMON_WILD_APPEARED), speciesName(pending.speciesId));
    centered(renderer, UI_12_FONT_ID, contentTop + 112, line, EpdFontFamily::BOLD);
    snprintf(line, sizeof(line), "%s %u    %s", tr(STR_POKEMON_LEVEL), pending.level, genderText(pending.gender));
    centered(renderer, UI_10_FONT_ID, contentTop + 140, line);
  } else if (pending.kind == pokemon::PendingEventKind::Item) {
    pokemon::drawPokemonItemArt(renderer, pending.item, true,
                                Rect{(renderer.getScreenWidth() - 120) / 2, contentTop + 8, 120, 90}, false);
    snprintf(line, sizeof(line), tr(STR_POKEMON_FOUND_ITEM), itemName(pending.item));
    centered(renderer, UI_12_FONT_ID, contentTop + 112, line, EpdFontFamily::BOLD);
  } else if (pending.kind == pokemon::PendingEventKind::Evolution) {
    pokemon::PokemonRecord record{};
    if (service_.readRecord(pending.recordId, record) != pokemon::ServiceStatus::Ok) return;
    pokemon::drawPokemonSpeciesArt(renderer, record.speciesId, true,
                                   Rect{(renderer.getScreenWidth() - 120) / 2, contentTop + 8, 120, 90});
    snprintf(line, sizeof(line), tr(STR_POKEMON_EVOLVING), speciesName(record.speciesId));
    centered(renderer, UI_12_FONT_ID, contentTop + 112, line, EpdFontFamily::BOLD);
  } else if (pending.kind == pokemon::PendingEventKind::MoveLearn) {
    pokemon::PokemonRecord record{};
    if (service_.readRecord(pending.recordId, record) != pokemon::ServiceStatus::Ok) return;
    pokemon::drawPokemonSpeciesArt(renderer, record.speciesId, true,
                                   Rect{(renderer.getScreenWidth() - 120) / 2, contentTop + 8, 120, 90});
    const pokemon::MoveData* newMove = pokemon::moveData(pending.speciesId);
    snprintf(line, sizeof(line), tr(STR_POKEMON_WANTS_TO_LEARN),
             record.nickname[0] == '\0' ? speciesName(record.speciesId) : record.nickname.data(),
             newMove == nullptr ? "?" : newMove->name);
    centered(renderer, UI_12_FONT_ID, contentTop + 112, line, EpdFontFamily::BOLD);
  }
}

void PokemonActivity::renderBattleHud() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + TouchHeaderBackButton::height(metrics, mappedInput) + 14;
  const int width = renderer.getScreenWidth();
  const int barWidth = width - 56;
  constexpr int barHeight = 14;

  const auto drawSide = [&](const pokemon::BattleCombatant& combatant, const int y, const bool isPlayer) {
    char line[64];
    snprintf(line, sizeof(line), "%s  %s%u", speciesName(combatant.speciesId), tr(STR_POKEMON_LEVEL), combatant.level);
    renderer.drawText(UI_10_FONT_ID, 28, y, line, true, EpdFontFamily::BOLD);
    renderer.drawRect(28, y + 20, barWidth, barHeight, true);
    const uint16_t maxHp = std::max<uint16_t>(1, combatant.maxHp);
    const int filled = combatant.maxHp == 0 ? 0 : (barWidth - 2) * combatant.currentHp / maxHp;
    if (filled > 0) renderer.fillRect(29, y + 21, filled, barHeight - 2, true);
    char hpText[32];
    snprintf(hpText, sizeof(hpText), "%u/%u", combatant.currentHp, combatant.maxHp);
    renderer.drawText(UI_10_FONT_ID, 28, y + 20 + barHeight + 4, hpText);
    if (combatant.status != pokemon::Ailment::None) {
      const char* status = statusAbbrev(combatant.status);
      renderer.drawText(UI_10_FONT_ID, width - 28 - renderer.getTextWidth(UI_10_FONT_ID, status, EpdFontFamily::BOLD),
                        y + 20 + barHeight + 4, status, true, EpdFontFamily::BOLD);
    }
    pokemon::drawPokemonSpeciesArt(renderer, combatant.speciesId, true,
                                   Rect{isPlayer ? 28 : width - 148, y + 50, 120, 90});
  };

  drawSide(battleOpponent_, contentTop, false);
  drawSide(battlePlayer_, contentTop + 160, true);
  if (battleLog_[0] != '\0') centered(renderer, UI_10_FONT_ID, contentTop + 330, battleLog_);
}

void PokemonActivity::renderRowArt() {
  const bool artRows = screen_ == Screen::Starter || screen_ == Screen::Party || screen_ == Screen::Move ||
                       screen_ == Screen::Pc || screen_ == Screen::BagEvolution || screen_ == Screen::ItemTarget ||
                       screen_ == Screen::Pokedex;
  if (!artRows) return;
  const int start = pageStart();
  for (int local = 0; local < rowCount_; ++local) {
    const int rowY = listBounds_.y + local * rowHeight_;
    uint16_t speciesId = 0;
    if (screen_ == Screen::Starter)
      speciesId = STARTERS[start + local];
    else if ((screen_ == Screen::Party || screen_ == Screen::Move || screen_ == Screen::ItemTarget) &&
             start + local < snapshot_.partyCount)
      speciesId = snapshot_.party[start + local].speciesId;
    else if (screen_ == Screen::Pc && local < static_cast<int>(pcCount_))
      speciesId = pcPage_[local].speciesId;
    else if (screen_ == Screen::Pokedex &&
             pokemon::isSpeciesMarked(snapshot_.state.seenSpecies, static_cast<uint16_t>(start + local + 1))) {
      speciesId = static_cast<uint16_t>(start + local + 1);
    }
    if (screen_ == Screen::BagEvolution) {
      // No icon assets exist for TM/HM/potions/etc - only the 6 evolution
      // stones (BagEvolution's rows) have art to draw here.
      constexpr int itemSize = 32;
      pokemon::drawPokemonItemArt(renderer, static_cast<pokemon::EvolutionItem>(start + local + 1), false,
                                  Rect{listBounds_.x + 5 + pokemon::pokemonCenteredOffset(80, itemSize),
                                       rowY + pokemon::pokemonCenteredOffset(rowHeight_, itemSize), itemSize, itemSize},
                                  false);
    } else if (speciesId != 0) {
      // The 40x30 menu files are intentionally native-sized and GfxRenderer
      // does not upscale. Use the same approved icon's 120x90 presentation
      // copy so it can be reduced cleanly into the row instead of appearing
      // as a tiny 40x30 mark on the X3 panel.
      pokemon::drawPokemonSpeciesArt(renderer, speciesId, true, Rect{listBounds_.x + 5, rowY + 2, 80, 60});
    }
  }
}

void PokemonActivity::renderHeaderAndHints() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (screen_ == Screen::PokedexDetail) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }
  const char* title = tr(STR_POKEMON);
  if (screen_ == Screen::Party || screen_ == Screen::Move || screen_ == Screen::ItemTarget)
    title = tr(STR_POKEMON_PARTY);
  else if (screen_ == Screen::Pc || screen_ == Screen::PcOrder)
    title = tr(STR_POKEMON_PC_BOX);
  else if (screen_ == Screen::Bag)
    title = tr(STR_POKEMON_BAG);
  else if (screen_ == Screen::BagEvolution)
    title = tr(STR_POKEMON_BAG_EVOLUTION);
  else if (screen_ == Screen::BagMedicine)
    title = tr(STR_POKEMON_BAG_MEDICINE);
  else if (screen_ == Screen::BagMachine)
    title = tr(STR_POKEMON_BAG_MACHINES);
  else if (screen_ == Screen::Pokedex)
    title = tr(STR_POKEDEX);
  else if (screen_ == Screen::Summary || screen_ == Screen::Actions)
    title = tr(STR_POKEMON_SUMMARY);
  else if (screen_ == Screen::Moveset || screen_ == Screen::MovesetPick || screen_ == Screen::TmReplaceSlot)
    title = tr(STR_POKEMON_MOVES);
  else if (screen_ == Screen::GymList)
    title = tr(STR_POKEMON_GYM_BATTLE);
  else if (screen_ == Screen::Badges)
    title = tr(STR_POKEMON_BADGES);
  const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInput);
  if (mappedInput.hasTouchHardware())
    TouchHeaderBackButton::draw(renderer, uiTarget_, header, title, false);
  else {
    // Let the theme draw its rule and battery, then place the title ourselves.
    // Some X3 font builds extend below their reported line cell; the standard
    // bottom-aligned title can therefore collide with the header rule.
    GUI.drawHeader(renderer, header, "");
    constexpr int titleRuleGap = 18;
    const int titleY = header.y + std::max(0, header.height - renderer.getLineHeight(UI_12_FONT_ID) - titleRuleGap);
    renderer.drawText(UI_12_FONT_ID, header.x + metrics.headerSidePadding, titleY, title, true, EpdFontFamily::BOLD);
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void PokemonActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderHeaderAndHints();
  uiReady_ = false;
  app_.render();
  uiReady_ = true;
  renderFocused();
  renderRowArt();
  renderer.displayBuffer(cleanRefreshNeeded_ ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  cleanRefreshNeeded_ = false;
}

#endif
