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
#include <vector>

#include "CrossPointSettings.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "components/icons/touchHeaderIcons.h"
#include "components/pokemon/PokemonArt.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_ROW = 1;
constexpr uint16_t STARTERS[] = {1, 4, 7, 25};
// Screen::Battle's own FIGHT/BALL/BAG/SWITCH/RUN command grid (2 columns to
// save vertical space - see renderBattleMenu()); shared with loop()'s
// grid-aware Up/Down/Left/Right navigation for that screen.
constexpr int BATTLE_MENU_COLUMNS = 2;
constexpr int BATTLE_MENU_ROW_HEIGHT = 64;
// Screen::Menu's own 2-column button grid (Party/Pokedex/PC Box/PC Sort/Bag/
// Gym Battle/Badges/Settings) - see renderMenuGrid(). Top-anchored (unlike
// the battle grid's bottom-anchored one, which sits below the battle HUD)
// since Menu has no HUD above it - just the normal header.
constexpr int MENU_GRID_COLUMNS = 2;
constexpr int MENU_GRID_ROW_HEIGHT = 64;
// An artwork list row reads left to right as: selection triangle (drawn by
// fui::list at markerInset=4, see pokemonListPresentation()), then the icon,
// then any text. This is the icon's left edge, offset far enough from the
// row's own left edge to clear that marker instead of the ~1px gap it used
// to leave (the marker used to sit *after* the icon, at markerInset=86,
// almost touching it).
constexpr int ROW_ICON_X = 24;

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

// Compact glyph for the Party/ItemTarget row's name/level/gender line
// (renderPartyRowHealth()) - the traditional ♂/♀ symbols weren't in this
// device's font (a fixed Latin/Hebrew/Arabic subset baked in at build time),
// so this used to fall back to "M"/"F". Both glyphs were added to the small
// shared ui_symbols_10 fallback font (already used for the power icon) from
// the existing NotoSansSymbols source, so the real symbols render now at
// UI_10/UI_12 alike - see lib/EpdFont/builtinFonts/ui_symbols_10.h's
// generation comment. Genderless/Unknown has nothing meaningful to show,
// matching genderText()'s own "" for Unknown.
const char* genderAbbrev(const pokemon::Gender gender) {
  if (gender == pokemon::Gender::Male) return "♂";
  if (gender == pokemon::Gender::Female) return "♀";
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
      // Stage 4 widened PendingEventKind::Item to hold any of the 83 items, not
      // just the original 6 evolution stones - those extra ids have no
      // i18n string (move/item names are intentionally not localized, see
      // the roadmap) and instead come straight from the generated data.
      const pokemon::ItemData* data = pokemon::itemData(static_cast<uint8_t>(item));
      return data == nullptr ? "" : data->name;
    }
  }
}

// The Bag is split into 3 categories, each its own screen: Evolution (the
// 6 stones + Link Cable, fixed positions unchanged since Stage 1), Medicine
// (heal/status-cure/PP-restore/candy items - Stage 9), and Machine (TM/HM,
// Stage 7). Ball items have no Bag row at all - they are only ever consumed
// via BattleBalls. Ids are not contiguous by category in the data file, so
// these walk the table by predicate rather than assuming a fixed range.
bool isMachineCategory(const pokemon::ItemCategory category) { return category == pokemon::ItemCategory::Machine; }

bool isMedicineCategory(const pokemon::ItemCategory category) {
  return category == pokemon::ItemCategory::Medicine || category == pokemon::ItemCategory::StatusCure ||
         category == pokemon::ItemCategory::PPRestore || category == pokemon::ItemCategory::Candy;
}

// Same as isMedicineCategory but without Candy (Stage 18): Candy raises totalXp
// on the main PokemonState, which can change the active battler's max HP -
// syncing that into the live in-RAM battlePlayer_ mid-fight is out of scope,
// so Candy stays an out-of-battle-only item.
bool isBattleUsableCategory(const pokemon::ItemCategory category) {
  return category == pokemon::ItemCategory::Medicine || category == pokemon::ItemCategory::StatusCure ||
         category == pokemon::ItemCategory::PPRestore;
}

// Stage 19 follow-up: every item menu now skips ids the player owns none of
// ("only show items actually owned") - decluttering was requested after
// Stage 19 added Bag > Balls right next to the other, often-empty categories.
// bagItemIdAt/bagItemCount take the live bagCounts so they can check
// ownership alongside category; ownedSlotAt/ownedSlotCount below do the
// same for the two screens that index a small fixed-size range directly
// (BagEvolution's 6 stones, Balls' 4 kinds) instead of walking the full
// item table by predicate.
uint8_t bagItemIdAt(const size_t index, const std::span<const uint8_t> bagCounts,
                    bool (*matches)(pokemon::ItemCategory)) {
  size_t count = 0;
  for (uint8_t id = pokemon::EVOLUTION_ITEM_COUNT + 1U; id <= pokemon::POKEMON_ITEM_ID_MAX; ++id) {
    const pokemon::ItemData* data = pokemon::itemData(id);
    if (data == nullptr || !matches(data->category)) continue;
    const auto bagIndex = static_cast<size_t>(id - pokemon::EVOLUTION_ITEM_COUNT - 1U);
    if (bagIndex >= bagCounts.size() || bagCounts[bagIndex] == 0) continue;
    if (count == index) return id;
    ++count;
  }
  return 0;
}

size_t bagItemCount(const std::span<const uint8_t> bagCounts, bool (*matches)(pokemon::ItemCategory)) {
  size_t count = 0;
  for (uint8_t id = pokemon::EVOLUTION_ITEM_COUNT + 1U; id <= pokemon::POKEMON_ITEM_ID_MAX; ++id) {
    const pokemon::ItemData* data = pokemon::itemData(id);
    if (data == nullptr || !matches(data->category)) continue;
    const auto bagIndex = static_cast<size_t>(id - pokemon::EVOLUTION_ITEM_COUNT - 1U);
    if (bagIndex >= bagCounts.size() || bagCounts[bagIndex] == 0) continue;
    ++count;
  }
  return count;
}

// Maps "the Nth item currently owned" back to its absolute index in a
// fixed-size count array (BagEvolution's 6-entry itemCounts, or the first 4
// entries of bagCounts for the 4 ball kinds) - returns -1 past the end.
int ownedSlotAt(const size_t index, const std::span<const uint8_t> counts) {
  size_t seen = 0;
  for (size_t i = 0; i < counts.size(); ++i) {
    if (counts[i] == 0) continue;
    if (seen == index) return static_cast<int>(i);
    ++seen;
  }
  return -1;
}

size_t ownedSlotCount(const std::span<const uint8_t> counts) {
  size_t total = 0;
  for (const uint8_t count : counts) {
    if (count != 0) ++total;
  }
  return total;
}

// BagEvolution's itemCounts is uint16_t (not uint8_t like bagCounts) - same
// logic, just a second overload since span deduction won't implicitly widen
// std::array<uint16_t, N> to std::span<const uint8_t>.
int ownedSlotAt(const size_t index, const std::span<const uint16_t> counts) {
  size_t seen = 0;
  for (size_t i = 0; i < counts.size(); ++i) {
    if (counts[i] == 0) continue;
    if (seen == index) return static_cast<int>(i);
    ++seen;
  }
  return -1;
}

size_t ownedSlotCount(const std::span<const uint16_t> counts) {
  size_t total = 0;
  for (const uint16_t count : counts) {
    if (count != 0) ++total;
  }
  return total;
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
// localized - like move and item names (Stage 1 decision), these read the same
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
  // A crit is orthogonal to the effectiveness suffix above (a hit can be
  // both critical and super-effective at once), so it's appended as its own
  // clause rather than folded into the ternary chain.
  if (suffix[0] == '\0' && !action.critical) {
    snprintf(buffer, size, "%s", used);
  } else if (!action.critical) {
    snprintf(buffer, size, "%s %s", used, suffix);
  } else if (suffix[0] == '\0') {
    snprintf(buffer, size, "%s %s", used, tr(STR_POKEMON_CRITICAL_HIT));
  } else {
    snprintf(buffer, size, "%s %s %s", used, tr(STR_POKEMON_CRITICAL_HIT), suffix);
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
  // Pokemon is portrait-only by design - force it on entry the same way
  // NearbyBookTransferActivity/SettingsActivity/SleepActivity do, so a
  // device left in landscape by the reader (or, in the simulator, by
  // CROSSINK_SIMULATOR_POKEMON_LANDSCAPE) never shows the unsupported
  // landscape layout.
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
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
  // Screen::Battle/BattleMoves/Menu/Bag draw their own 2-column button grids
  // (renderBattleMenu()/renderBattleMoveMenu()/renderMenuGrid()/
  // renderBagGrid()) instead of the generic single-column list, and
  // Screen::PcOrder draws a single-column button variant (renderPcOrderButtons())
  // - see the comments there. Screen::Pc itself deliberately stays a
  // single-column list (with a species icon per row, see artRows below) - a
  // grid there left the screen looking too empty per user feedback, so only
  // Bag/Menu/PcOrder kept the button treatment.
  return screen_ != Screen::Summary && screen_ != Screen::PokedexDetail && screen_ != Screen::Message &&
         screen_ != Screen::Battle && screen_ != Screen::BattleMoves && screen_ != Screen::Menu &&
         screen_ != Screen::Bag && screen_ != Screen::PcOrder;
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
      return 8;
    case Screen::Settings:
      return 2;
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
      // whether there's anything new to learn (Stage 12).
      return static_cast<int>(learnableMoveCount(record.speciesId, pokemon::levelForXp(record.totalXp), entry)) + 1;
    }
    case Screen::TmReplaceSlot:
      return pokemon::BATTLE_MOVE_SLOTS + 1;
    case Screen::Pc:
      return static_cast<int>(snapshot_.ownedCount - snapshot_.partyCount);
    case Screen::PcOrder:
      return 3;
    case Screen::Bag:
      return 4;
    case Screen::BagEvolution:
      return static_cast<int>(ownedSlotCount(snapshot_.state.itemCounts));
    case Screen::BagMedicine:
      return static_cast<int>(bagItemCount(snapshot_.state.bagCounts, isMedicineCategory));
    case Screen::BagBalls:
      return static_cast<int>(ownedSlotCount(std::span<const uint8_t>(snapshot_.state.bagCounts).first(4)));
    case Screen::BagMachine:
      return static_cast<int>(bagItemCount(snapshot_.state.bagCounts, isMachineCategory));
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
      // FIGHT+BAG+SWITCH+RUN for a trainer battle (no BALL option);
      // FIGHT+BALL+BAG+SWITCH+RUN for a wild encounter.
      return gymChallengeIndex_ != 0 ? 4 : 5;
    case Screen::BattleMoves:
      return battlePlayerMoveCount();
    case Screen::BattleBalls:
      return static_cast<int>(ownedSlotCount(std::span<const uint8_t>(snapshot_.state.bagCounts).first(4)));
    case Screen::BattleBag:
      return static_cast<int>(bagItemCount(snapshot_.state.bagCounts, isBattleUsableCategory));
    case Screen::BattleSwitch:
      return static_cast<int>(usablePartySlotCount());
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

// Rows get extra height to fit an HP bar/text/status strip below the usual
// icon+name+level line (see renderPartyRowHealth()) - every other list rides
// the standard row height. True for Screen::Party; for Screen::ItemTarget
// only while picking who receives a Medicine/BattleMedicine item
// (Evolution/Machine targets have no HP to show); and for
// Screen::BattleSwitch, where seeing HP/status is exactly what decides
// which Pokemon to send out.
bool PokemonActivity::showsPartyHealthRows() const {
  if (screen_ == Screen::Party || screen_ == Screen::BattleSwitch) return true;
  return screen_ == Screen::ItemTarget &&
         (bagCategory_ == BagCategory::Medicine || bagCategory_ == BagCategory::BattleMedicine);
}

int PokemonActivity::rowHeightForScreen() const { return showsPartyHealthRows() ? 96 : 64; }

int PokemonActivity::rowsPerPage() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int bottomReserve = metrics.buttonHintsHeight + 8;
  return pokemon::pokemonRowsPerPage(renderer.getScreenHeight(), listTop(), bottomReserve, rowHeightForScreen(),
                                     ROW_CAPACITY);
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

int PokemonActivity::firstUsablePartySlot() const {
  for (int slot = 0; slot < snapshot_.partyCount; ++slot) {
    if (service_.peekBattleMoves(snapshot_.party[slot]).currentHp > 0) return slot;
  }
  return -1;
}

size_t PokemonActivity::usablePartySlotCount() const {
  size_t count = 0;
  for (int slot = 0; slot < snapshot_.partyCount; ++slot) {
    if (slot == battlePartySlot_) continue;
    if (service_.peekBattleMoves(snapshot_.party[slot]).currentHp > 0) ++count;
  }
  return count;
}

int PokemonActivity::usablePartySlotAt(const size_t index) const {
  size_t count = 0;
  for (int slot = 0; slot < snapshot_.partyCount; ++slot) {
    if (slot == battlePartySlot_) continue;
    if (service_.peekBattleMoves(snapshot_.party[slot]).currentHp == 0) continue;
    if (count == index) return slot;
    ++count;
  }
  return -1;
}

bool PokemonActivity::setupBattlePlayer(const int slot) {
  if (slot < 0 || slot >= snapshot_.partyCount) return false;
  const pokemon::PokemonRecord& fighter = snapshot_.party[slot];
  pokemon::BattleRecordEntry entry{};
  if (service_.loadBattleEntry(fighter.recordId, entry) != pokemon::ServiceStatus::Ok) return false;

  battlePartySlot_ = slot;
  battlePlayer_ = pokemon::BattleCombatant{};
  battlePlayer_.speciesId = fighter.speciesId;
  battlePlayer_.gender = fighter.gender;
  battlePlayer_.level = pokemon::levelForXp(fighter.totalXp);
  const pokemon::BaseStats* playerStats = pokemon::baseStatsFor(fighter.speciesId);
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
                                          const std::span<const uint8_t> fixedMoves, const pokemon::Gender gender) {
  battleOpponent_ = pokemon::BattleCombatant{};
  battleOpponent_.speciesId = speciesId;
  battleOpponent_.level = level;
  battleOpponent_.gender = gender;
  const pokemon::BaseStats* stats = pokemon::baseStatsFor(speciesId);
  battleOpponent_.maxHp = stats == nullptr ? 1 : pokemon::battleMaxHp(stats->hp, level);
  battleOpponent_.currentHp = battleOpponent_.maxHp;
  if (!fixedMoves.empty()) {
    // Gym/Elite Four trainer - real Pokemon Red teams never derive their
    // moves from the learnset-by-level table, so this comes straight from
    // GymTeamMember::moves (Stage 12) instead of defaultMovesetForLevel().
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
  const int slot = firstUsablePartySlot();
  if (slot < 0 || !setupBattlePlayer(slot)) return false;
  setupBattleOpponent(pending.speciesId, pending.level, {}, pending.gender);
  gymChallengeIndex_ = 0;
  forcedBattleSwitch_ = false;
  snprintf(battleLog_, sizeof(battleLog_), tr(STR_POKEMON_GO), speciesName(battlePlayer_.speciesId));
  setScreen(Screen::Battle);
  return true;
}

bool PokemonActivity::enterGymBattle(const uint8_t gymIndex) {
  const auto team = pokemon::gymTeamFor(gymIndex);
  if (team.empty()) return false;
  const int slot = firstUsablePartySlot();
  if (slot < 0 || !setupBattlePlayer(slot)) return false;
  setupBattleOpponent(team[0].speciesId, team[0].level, team[0].moves, service_.rollGenderFor(team[0].speciesId));
  gymChallengeIndex_ = gymIndex;
  gymChallengeTeamProgress_ = 0;
  forcedBattleSwitch_ = false;
  const pokemon::GymData* gym = pokemon::gymData(gymIndex);
  char sentOut[96];
  snprintf(sentOut, sizeof(sentOut), tr(STR_POKEMON_SENT_OUT), gym == nullptr ? "?" : gym->leaderName,
           speciesName(battleOpponent_.speciesId));
  char go[64];
  snprintf(go, sizeof(go), tr(STR_POKEMON_GO), speciesName(battlePlayer_.speciesId));
  snprintf(battleLog_, sizeof(battleLog_), "%s\n%s", sentOut, go);
  setScreen(Screen::Battle);
  return true;
}

void PokemonActivity::savePlayerBattleEntry() {
  if (battlePartySlot_ < 0 || battlePartySlot_ >= snapshot_.partyCount) return;
  pokemon::BattleRecordEntry entry{};
  entry.recordId = snapshot_.party[battlePartySlot_].recordId;
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
  // Award XP to whichever Pokemon is actively fighting before anything below
  // switches battleOpponent_ to the gym's next team member or clears
  // battlePartySlot_ - both branches (wild faint and one gym opponent down)
  // reach here with the just-defeated opponent's level still valid.
  if (battlePartySlot_ >= 0 && battlePartySlot_ < snapshot_.partyCount) {
    service_.awardBattleXp(snapshot_.party[battlePartySlot_].recordId, battleOpponent_.level,
                           gymChallengeIndex_ != 0);
  }
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
    pokemon::GymTeamMember next = team[gymChallengeTeamProgress_];
    if (gymChallengeIndex_ == pokemon::CHAMPION_GYM_INDEX && gymChallengeTeamProgress_ == team.size() - 1) {
      // The Champion's final slot sends out the evolution that counters
      // the player's own starter in the real games - see
      // championFinalSlotFor(). recordId 1 is always the starter
      // (createStarter() hardcodes it), so this is a plain lookup rather
      // than tracking a separate "starter species" field.
      pokemon::PokemonRecord starterRecord{};
      if (service_.readRecord(1, starterRecord) == pokemon::ServiceStatus::Ok) {
        next = pokemon::championFinalSlotFor(starterRecord.speciesId);
      }
    }
    setupBattleOpponent(next.speciesId, next.level, next.moves, service_.rollGenderFor(next.speciesId));
    const pokemon::GymData* gym = pokemon::gymData(gymChallengeIndex_);
    snprintf(battleLog_, sizeof(battleLog_), tr(STR_POKEMON_SENT_OUT), gym == nullptr ? "?" : gym->leaderName,
             speciesName(battleOpponent_.speciesId));
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
      else
        setScreen(Screen::Settings);
      return;
    case Screen::Settings:
      if (selected_ == 0) {
        const uint8_t previous = SETTINGS.pokemonHomeScreen;
        SETTINGS.pokemonHomeScreen = previous == 0 ? 1 : 0;
        if (!SETTINGS.saveToFile()) {
          SETTINGS.pokemonHomeScreen = previous;
          showMessage(tr(STR_POKEMON_SAVE_ERROR), Screen::Settings);
        } else {
          setScreen(Screen::Settings, selected_);
        }
      } else {
        setScreen(Screen::ResetFirst);
      }
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
        // anything (Stage 12; refuses to clear a Pokemon's last move).
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
      setScreen(selected_ == 0   ? Screen::BagEvolution
                : selected_ == 1 ? Screen::BagMedicine
                : selected_ == 2 ? Screen::BagBalls
                                 : Screen::BagMachine);
      return;
    case Screen::BagBalls:
      // View-only: balls only do anything mid-battle against a wild
      // Pokemon (Screen::BattleBalls), which is a completely different
      // screen/context - there's nothing meaningful to "activate" here.
      showMessage(tr(STR_POKEMON_BAG_BALLS_INFO), Screen::BagBalls);
      return;
    case Screen::BagEvolution: {
      // Zero-count stones are already filtered out of the list (Stage 19
      // follow-up), so selected_ is a row index among owned stones only -
      // map it back to the real stone slot.
      const int slot = ownedSlotAt(static_cast<size_t>(selected_), snapshot_.state.itemCounts);
      if (slot < 0) return;
      bagCategory_ = BagCategory::Evolution;
      selectedItem_ = static_cast<pokemon::EvolutionItem>(slot + 1);
      setScreen(Screen::ItemTarget);
      return;
    }
    case Screen::BagMedicine: {
      bagCategory_ = BagCategory::Medicine;
      const uint8_t itemId = bagItemIdAt(static_cast<size_t>(selected_), snapshot_.state.bagCounts, isMedicineCategory);
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
      const uint8_t itemId = bagItemIdAt(static_cast<size_t>(selected_), snapshot_.state.bagCounts, isMachineCategory);
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
      const Screen bagScreen = bagCategory_ == BagCategory::Evolution        ? Screen::BagEvolution
                               : bagCategory_ == BagCategory::Medicine       ? Screen::BagMedicine
                               : bagCategory_ == BagCategory::BattleMedicine ? Screen::BattleBag
                                                                             : Screen::BagMachine;
      if (bagCategory_ == BagCategory::BattleMedicine) {
        const pokemon::UseConsumableOutcome outcome = service_.useConsumable(recordId, selectedMedicineItemId_);
        if (outcome == pokemon::UseConsumableOutcome::NotApplicable) {
          showMessage(tr(STR_POKEMON_NOT_APPLICABLE), bagScreen);
          return;
        }
        if (outcome != pokemon::UseConsumableOutcome::Applied ||
            service_.consumeBagItem(selectedMedicineItemId_) != pokemon::ServiceStatus::Ok) {
          showMessage(tr(STR_POKEMON_SAVE_ERROR), bagScreen);
          return;
        }
        // useConsumable() persists straight to the on-disk BattleRecordEntry
        // via recordId - it never touches the live in-RAM battlePlayer_ that
        // stepBattle()/renderBattleHud() actually read. If the target was
        // the active combatant, pull the entry back and copy it in, the
        // same fields setupBattlePlayer() seeds at the start of a fight.
        pokemon::PokemonRecord targetRecord{};
        const bool isActiveCombatant = battlePartySlot_ >= 0 && battlePartySlot_ < snapshot_.partyCount &&
                                       recordId == snapshot_.party[battlePartySlot_].recordId;
        if (isActiveCombatant && service_.readRecord(recordId, targetRecord) == pokemon::ServiceStatus::Ok) {
          const pokemon::BattleRecordEntry entry = service_.peekBattleMoves(targetRecord);
          battlePlayer_.currentHp = entry.currentHp;
          battlePlayer_.status = entry.status;
          battlePlayer_.statusTurns = entry.statusTurns;
          for (size_t i = 0; i < pokemon::BATTLE_MOVE_SLOTS; ++i) battlePlayer_.moves[i].currentPp = entry.pp[i];
        }
        if (!refreshSnapshot()) return;
        const pokemon::ItemData* item = pokemon::itemData(selectedMedicineItemId_);
        char usedLine[80];
        snprintf(usedLine, sizeof(usedLine), tr(STR_POKEMON_USED_ITEM), speciesName(battlePlayer_.speciesId),
                 item == nullptr ? "?" : item->name);
        // Using an item mid-battle costs the whole turn in Gen 1 - the
        // opponent attacks the active Pokemon right away regardless of
        // which party member received the item, with no Speed comparison
        // (see stepOpponentOnlyTurn()).
        const pokemon::BattleTurnResult result = service_.resolveOpponentOnlyTurn(battlePlayer_, battleOpponent_);
        savePlayerBattleEntry();
        char opponentLine[80] = "";
        if (result.opponent.acted) {
          formatBattleActionLine(opponentLine, sizeof(opponentLine), battleOpponent_, result.opponent);
        }
        if (opponentLine[0] != '\0') {
          snprintf(battleLog_, sizeof(battleLog_), "%s\n%s", usedLine, opponentLine);
        } else {
          snprintf(battleLog_, sizeof(battleLog_), "%s", usedLine);
        }
        if (result.outcome == pokemon::BattleOutcome::OpponentWon) {
          if (usablePartySlotCount() > 0) {
            forcedBattleSwitch_ = true;
            setScreen(Screen::BattleSwitch);
          } else {
            finishBattleAfterPlayerFainted();
          }
          return;
        }
        setScreen(Screen::Battle);
        return;
      }
      if (bagCategory_ == BagCategory::Machine) {
        const pokemon::TeachMoveOutcome outcome = service_.teachMove(recordId, selectedMachineMoveId_);
        if (outcome == pokemon::TeachMoveOutcome::AlreadyKnown) {
          showMessage(tr(STR_POKEMON_ALREADY_KNOWS_MOVE), bagScreen);
        } else if (outcome == pokemon::TeachMoveOutcome::Incompatible) {
          showMessage(tr(STR_POKEMON_CANNOT_LEARN_MACHINE), bagScreen);
        } else if (outcome == pokemon::TeachMoveOutcome::MovesetFull) {
          // Full moveset no longer just blocks the TM - let the player
          // choose which of the 4 current moves to overwrite (Stage 12).
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
          if (firstUsablePartySlot() < 0) {
            showMessage(tr(STR_POKEMON_NO_USABLE_POKEMON), Screen::Event);
          } else if (!enterBattle(pending)) {
            showMessage(tr(STR_POKEMON_SAVE_ERROR), Screen::Event);
          }
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
    case Screen::Battle: {
      const bool isGym = gymChallengeIndex_ != 0;
      if (selected_ == 0) {
        if (battlePlayerMoveCount() == 0) {
          showMessage(tr(STR_POKEMON_NOT_APPLICABLE), Screen::Battle);
          return;
        }
        setScreen(Screen::BattleMoves);
        return;
      }
      if (!isGym && selected_ == 1) {
        // Trainer battles (gym/Elite Four) have no BALL option - their
        // logicalCount() is 4 (FIGHT/BAG/SWITCH/RUN), so selected_ == 1
        // there is already BAG, handled by the shared branch below.
        if (ownedSlotCount(std::span<const uint8_t>(snapshot_.state.bagCounts).first(4)) == 0) {
          showMessage(tr(STR_POKEMON_NOT_APPLICABLE), Screen::Battle);
          return;
        }
        setScreen(Screen::BattleBalls);
        return;
      }
      if (selected_ == (isGym ? 1 : 2)) {
        if (bagItemCount(snapshot_.state.bagCounts, isBattleUsableCategory) == 0) {
          showMessage(tr(STR_POKEMON_NOT_APPLICABLE), Screen::Battle);
          return;
        }
        setScreen(Screen::BattleBag);
        return;
      }
      if (selected_ == (isGym ? 2 : 3)) {
        if (usablePartySlotCount() == 0) {
          showMessage(tr(STR_POKEMON_NO_OTHER_USABLE), Screen::Battle);
          return;
        }
        forcedBattleSwitch_ = false;
        setScreen(Screen::BattleSwitch);
        return;
      }
      resolveBattleAsPass();  // RUN - always the last option either way
      return;
    }
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
        // Only truly a loss once nothing left in the party can fight -
        // otherwise force a switch instead of ending the battle (Stage 13).
        if (usablePartySlotCount() > 0) {
          forcedBattleSwitch_ = true;
          setScreen(Screen::BattleSwitch);
        } else {
          finishBattleAfterPlayerFainted();
        }
      } else {
        setScreen(Screen::Battle);
      }
      return;
    }
    case Screen::BattleSwitch: {
      const int slot = usablePartySlotAt(static_cast<size_t>(selected_));
      if (slot < 0) return;
      // A forced switch (the previous Pokemon just fainted from the
      // opponent's attack THIS turn) doesn't cost another one - only a
      // voluntary switch does. Capture this before setupBattlePlayer()
      // resets the flag below.
      const bool wasForced = forcedBattleSwitch_;
      savePlayerBattleEntry();
      if (!setupBattlePlayer(slot)) {
        showMessage(tr(STR_POKEMON_SAVE_ERROR), Screen::Battle);
        return;
      }
      forcedBattleSwitch_ = false;
      char goLine[64];
      snprintf(goLine, sizeof(goLine), tr(STR_POKEMON_GO), speciesName(battlePlayer_.speciesId));
      if (wasForced) {
        snprintf(battleLog_, sizeof(battleLog_), "%s", goLine);
        setScreen(Screen::Battle);
        return;
      }
      // Voluntary switch: Gen 1 spends the whole turn on it, so the
      // opponent gets to attack the newly-sent-out Pokemon right away, with
      // no Speed comparison (see stepOpponentOnlyTurn()).
      const pokemon::BattleTurnResult result = service_.resolveOpponentOnlyTurn(battlePlayer_, battleOpponent_);
      savePlayerBattleEntry();
      char opponentLine[80] = "";
      if (result.opponent.acted) {
        formatBattleActionLine(opponentLine, sizeof(opponentLine), battleOpponent_, result.opponent);
      }
      if (opponentLine[0] != '\0') {
        snprintf(battleLog_, sizeof(battleLog_), "%s\n%s", goLine, opponentLine);
      } else {
        snprintf(battleLog_, sizeof(battleLog_), "%s", goLine);
      }
      if (result.outcome == pokemon::BattleOutcome::OpponentWon) {
        if (usablePartySlotCount() > 0) {
          forcedBattleSwitch_ = true;
          setScreen(Screen::BattleSwitch);
        } else {
          finishBattleAfterPlayerFainted();
        }
        return;
      }
      setScreen(Screen::Battle);
      return;
    }
    case Screen::BattleBag: {
      const uint8_t itemId =
          bagItemIdAt(static_cast<size_t>(selected_), snapshot_.state.bagCounts, isBattleUsableCategory);
      const auto bagIndex = static_cast<size_t>(itemId - pokemon::EVOLUTION_ITEM_COUNT - 1U);
      if (itemId == 0 || bagIndex >= snapshot_.state.bagCounts.size() || snapshot_.state.bagCounts[bagIndex] == 0) {
        showMessage(tr(STR_POKEMON_NOT_APPLICABLE), Screen::BattleBag);
        return;
      }
      // Like the out-of-battle Bag, picking an item doesn't use it on the
      // active combatant right away - it goes to Screen::ItemTarget to pick
      // which of the up-to-6 party members receives it (a benched Pokemon
      // can be healed mid-fight too, not just the one currently battling).
      bagCategory_ = BagCategory::BattleMedicine;
      selectedMedicineItemId_ = itemId;
      setScreen(Screen::ItemTarget);
      return;
    }
    case Screen::BattleBalls: {
      // Defense-in-depth (Stage 19): the only path into this screen already
      // gates on !isGym (see Screen::Battle's selection handler above), so
      // this should never be true - but a ball thrown at a trainer's
      // Pokemon would be a real gameplay bug, not just a cosmetic one, so
      // this is worth guarding directly rather than trusting the one call
      // site to always stay correct as the menu keeps changing (Stage 18 added
      // BAG in between BALL and SWITCH, for example).
      if (gymChallengeIndex_ != 0) {
        setScreen(Screen::Battle);
        return;
      }
      if (selected_ < 0) return;
      // Zero-count ball kinds are already filtered out of the list (Stage 19
      // follow-up), so selected_ is a row index among owned kinds only -
      // map it back to the real ball kind/bagCounts slot.
      const int ballSlot =
          ownedSlotAt(static_cast<size_t>(selected_), std::span<const uint8_t>(snapshot_.state.bagCounts).first(4));
      if (ballSlot < 0) return;
      const auto ballItemIndex = static_cast<size_t>(ballSlot);
      const auto ball = static_cast<pokemon::BallKind>(ballSlot);
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
      // A successful catch counts as a win too - award the same battle XP a
      // faint would have (always the wild multiplier; BattleBalls is gym-gated
      // out above, so this path is never a trainer battle).
      if (battlePartySlot_ >= 0 && battlePartySlot_ < snapshot_.partyCount) {
        service_.awardBattleXp(snapshot_.party[battlePartySlot_].recordId, battleOpponent_.level, false);
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
      if (firstUsablePartySlot() < 0) {
        showMessage(tr(STR_POKEMON_NO_USABLE_POKEMON), Screen::GymList);
      } else if (!enterGymBattle(gymIndex)) {
        showMessage(tr(STR_POKEMON_SAVE_ERROR), Screen::GymList);
      }
      return;
    }
    case Screen::Badges:
      return;  // display-only, nothing to activate
    case Screen::ResetFirst:
      if (selected_ == 0)
        setScreen(Screen::ResetFinal);
      else
        setScreen(Screen::Settings);
      return;
    case Screen::ResetFinal:
      if (selected_ == 0 && service_.reset() == pokemon::ServiceStatus::Ok) {
        snapshot_ = {};
        setScreen(Screen::Starter);
      } else if (selected_ == 0)
        showMessage(tr(STR_POKEMON_SAVE_ERROR), Screen::Settings);
      else
        setScreen(Screen::Settings);
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
      setScreen(bagCategory_ == BagCategory::Evolution        ? Screen::BagEvolution
                : bagCategory_ == BagCategory::Medicine       ? Screen::BagMedicine
                : bagCategory_ == BagCategory::BattleMedicine ? Screen::BattleBag
                                                              : Screen::BagMachine);
      return;
    case Screen::BagEvolution:
    case Screen::BagMedicine:
    case Screen::BagBalls:
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
    case Screen::BattleBag:
      setScreen(Screen::Battle);
      return;
    case Screen::BattleSwitch:
      // A forced switch (the active Pokemon just fainted) can't be
      // cancelled - Back is simply ignored until a replacement is chosen.
      if (!forcedBattleSwitch_) setScreen(Screen::Battle);
      return;
    case Screen::PokedexDetail:
      setScreen(Screen::Pokedex, static_cast<int>(pokedexSpecies_ - 1U));
      return;
    case Screen::Message:
      setScreen(returnScreen_);
      return;
    case Screen::ResetFirst:
    case Screen::ResetFinal:
      setScreen(Screen::Settings);
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
  // Screen::Message has no list/grid to route a tap into (logicalCount() is
  // 0), so without this it could only be dismissed by the physical Confirm
  // button or by leaving via the header's Back tap - not by tapping the
  // message itself, unlike every other screen. wasScreenTapped() is a no-op
  // stub on CAP_TOUCH=0 builds (X3), so this compiles to nothing there.
  if (screen_ == Screen::Message) {
    int x = 0, y = 0;
    if (mappedInput.wasScreenTapped(x, y)) {
      activate();
      return;
    }
  }
  const int count = logicalCount();
  if (count <= 0) return;
  if (screen_ == Screen::Battle || screen_ == Screen::BattleMoves) {
    // These two screens bypass buildList()/fui::list() entirely (they draw
    // their own 2-column grid - see renderBattleMenu()/renderBattleMoveMenu()),
    // so the generic app_.route() touch dispatch above never sees them. Hit-
    // test each cell using the exact same rect the render functions draw
    // into (battleGridCellRect()), so a tap can never land on a cell the
    // drawing doesn't agree with. wasTapInRect() is a no-op stub on
    // CAP_TOUCH=0 builds (X3), so this compiles to nothing there.
    if (mappedInput.hasTouchHardware()) {
      for (int index = 0; index < count; ++index) {
        const Rect cell = battleGridCellRect(index);
        if (mappedInput.wasTapInRect(cell.x, cell.y, cell.width, cell.height)) {
          selected_ = index;
          activate();
          return;
        }
      }
    }
    // 2-column grid (renderBattleMenu()/renderBattleMoveMenu()) instead of a
    // single-column list: Left/Right step through in reading order same as
    // everywhere else; Up/Down jump by BATTLE_MENU_COLUMNS to move within
    // the same column, wrapping to the column's other end - a plain
    // page-jump (what every other screen uses) doesn't make sense for a
    // 2-column grid.
    const auto moveBattle = [this](const int next) {
      selected_ = next;
      requestUpdate();
    };
    navigator_.onPressAndContinuous({MappedInputManager::Button::Right},
                                    [this, count, &moveBattle] { moveBattle((selected_ + 1) % count); });
    navigator_.onPressAndContinuous({MappedInputManager::Button::Left},
                                    [this, count, &moveBattle] { moveBattle((selected_ - 1 + count) % count); });
    navigator_.onPressAndContinuous({MappedInputManager::Button::Down}, [this, count, &moveBattle] {
      int next = selected_ + BATTLE_MENU_COLUMNS;
      if (next >= count) next = selected_ % BATTLE_MENU_COLUMNS;  // wrap to this column's top row
      moveBattle(next);
    });
    navigator_.onPressAndContinuous({MappedInputManager::Button::Up}, [this, count, &moveBattle] {
      int prev = selected_ - BATTLE_MENU_COLUMNS;
      if (prev < 0) {
        const int column = selected_ % BATTLE_MENU_COLUMNS;
        const int lastRowStart = ((count - 1) / BATTLE_MENU_COLUMNS) * BATTLE_MENU_COLUMNS;
        prev = lastRowStart + column;
        if (prev >= count) prev -= BATTLE_MENU_COLUMNS;  // last row is short a column
      }
      moveBattle(prev);
    });
    return;
  }
  if (screen_ == Screen::Menu) {
    // Same reasoning as the Battle grid above: bypasses buildList()/
    // fui::list() entirely (see isListScreen()), so it needs its own touch
    // hit-test and grid-aware Up/Down/Left/Right navigation instead of the
    // generic list dispatch/page-jump. wasTapInRect() is a no-op stub on
    // CAP_TOUCH=0 builds (X3), so this compiles to nothing there.
    if (mappedInput.hasTouchHardware()) {
      for (int index = 0; index < count; ++index) {
        const Rect cell = buttonGridCellRect(index);
        if (mappedInput.wasTapInRect(cell.x, cell.y, cell.width, cell.height)) {
          selected_ = index;
          activate();
          return;
        }
      }
    }
    const auto moveMenu = [this](const int next) {
      selected_ = next;
      requestUpdate();
    };
    navigator_.onPressAndContinuous({MappedInputManager::Button::Right},
                                    [this, count, &moveMenu] { moveMenu((selected_ + 1) % count); });
    navigator_.onPressAndContinuous({MappedInputManager::Button::Left},
                                    [this, count, &moveMenu] { moveMenu((selected_ - 1 + count) % count); });
    navigator_.onPressAndContinuous({MappedInputManager::Button::Down}, [this, count, &moveMenu] {
      int next = selected_ + MENU_GRID_COLUMNS;
      if (next >= count) next = selected_ % MENU_GRID_COLUMNS;  // wrap to this column's top row
      moveMenu(next);
    });
    navigator_.onPressAndContinuous({MappedInputManager::Button::Up}, [this, count, &moveMenu] {
      int prev = selected_ - MENU_GRID_COLUMNS;
      if (prev < 0) {
        const int column = selected_ % MENU_GRID_COLUMNS;
        const int lastRowStart = ((count - 1) / MENU_GRID_COLUMNS) * MENU_GRID_COLUMNS;
        prev = lastRowStart + column;
        if (prev >= count) prev -= MENU_GRID_COLUMNS;  // last row is short a column
      }
      moveMenu(prev);
    });
    return;
  }
  if (screen_ == Screen::Bag) {
    // Same reasoning as the Menu grid above - bypasses buildList()/
    // fui::list() (see isListScreen()). Always exactly 4 entries (one page),
    // so the same column-wrap Up/Down as Menu, no pagination needed (unlike
    // Pc below).
    if (mappedInput.hasTouchHardware()) {
      for (int index = 0; index < count; ++index) {
        const Rect cell = buttonGridCellRect(index);
        if (mappedInput.wasTapInRect(cell.x, cell.y, cell.width, cell.height)) {
          selected_ = index;
          activate();
          return;
        }
      }
    }
    const auto moveBag = [this](const int next) {
      selected_ = next;
      requestUpdate();
    };
    navigator_.onPressAndContinuous({MappedInputManager::Button::Right},
                                    [this, count, &moveBag] { moveBag((selected_ + 1) % count); });
    navigator_.onPressAndContinuous({MappedInputManager::Button::Left},
                                    [this, count, &moveBag] { moveBag((selected_ - 1 + count) % count); });
    navigator_.onPressAndContinuous({MappedInputManager::Button::Down}, [this, count, &moveBag] {
      int next = selected_ + MENU_GRID_COLUMNS;
      if (next >= count) next = selected_ % MENU_GRID_COLUMNS;  // wrap to this column's top row
      moveBag(next);
    });
    navigator_.onPressAndContinuous({MappedInputManager::Button::Up}, [this, count, &moveBag] {
      int prev = selected_ - MENU_GRID_COLUMNS;
      if (prev < 0) {
        const int column = selected_ % MENU_GRID_COLUMNS;
        const int lastRowStart = ((count - 1) / MENU_GRID_COLUMNS) * MENU_GRID_COLUMNS;
        prev = lastRowStart + column;
        if (prev >= count) prev -= MENU_GRID_COLUMNS;  // last row is short a column
      }
      moveBag(prev);
    });
    return;
  }
  if (screen_ == Screen::PcOrder) {
    // Same reasoning as Menu/Bag above - bypasses buildList()/fui::list()
    // (see isListScreen()). A single column, so Up/Down/Left/Right all just
    // step by 1 through the 3 entries (no column math needed).
    if (mappedInput.hasTouchHardware()) {
      for (int index = 0; index < count; ++index) {
        const Rect cell = buttonGridCellRect(index, 1);
        if (mappedInput.wasTapInRect(cell.x, cell.y, cell.width, cell.height)) {
          selected_ = index;
          activate();
          return;
        }
      }
    }
    const auto movePcOrder = [this](const int next) {
      selected_ = next;
      requestUpdate();
    };
    navigator_.onPressAndContinuous(ButtonNavigator::getNextButtons(),
                                    [this, count, &movePcOrder] { movePcOrder((selected_ + 1) % count); });
    navigator_.onPressAndContinuous(ButtonNavigator::getPreviousButtons(),
                                    [this, count, &movePcOrder] { movePcOrder((selected_ - 1 + count) % count); });
    return;
  }
  const int perPage = rowsPerPage();
  // Swipe up/down pages through long lists (Bag, Pokedex, PC box...) the same
  // way the physical Up/Down buttons already do below - the touch-only X4
  // Pro otherwise has no way to page through a list longer than one screen.
  // Swipe up moves forward (same direction as the physical Down button),
  // swipe down moves backward, matching the scroll convention used
  // elsewhere (e.g. NetworkModeSelectionActivity's list). wasSwipe() is a
  // constexpr no-op returning SwipeDir::None on CAP_TOUCH=0 builds (X3), so
  // this compiles to nothing there.
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    selected_ = swipe == MappedInputManager::SwipeDir::Up
                    ? ButtonNavigator::nextPageIndex(selected_, count, perPage)
                    : ButtonNavigator::previousPageIndex(selected_, count, perPage);
    requestUpdate();
    return;
  }
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
      // Unreachable in practice: isListScreen() excludes Screen::Menu (it
      // draws its own 2-column grid via renderMenuGrid(), which has its own
      // copy of these same 8 labels - see the comment there).
      case Screen::Menu:
        row(local, index == 0   ? tr(STR_POKEMON_PARTY)
                   : index == 1 ? tr(STR_POKEDEX)
                   : index == 2 ? tr(STR_POKEMON_PC_BOX)
                   : index == 3 ? tr(STR_POKEMON_PC_SORT)
                   : index == 4 ? tr(STR_POKEMON_BAG)
                   : index == 5 ? tr(STR_POKEMON_GYM_BATTLE)
                   : index == 6 ? tr(STR_POKEMON_BADGES)
                                : tr(STR_POKEMON_SETTINGS));
        break;
      case Screen::Settings:
        if (index == 0) {
          row(local, tr(STR_POKEMON_HOME_SCREEN),
              SETTINGS.pokemonHomeScreen != 0 ? tr(STR_POKEMON_ON) : tr(STR_POKEMON_OFF));
        } else {
          row(local, tr(STR_POKEMON_RESET));
        }
        break;
      case Screen::Party:
      case Screen::Move:
      case Screen::ItemTarget: {
        if (index >= snapshot_.partyCount) {
          row(local, tr(STR_POKEMON_EMPTY));
          break;
        }
        if (showsPartyHealthRows() && (screen_ == Screen::Party || screen_ == Screen::ItemTarget)) {
          // Name/level/gender is drawn entirely by renderPartyRowHealth()
          // alongside the HP bar instead - that keeps both lines sharing one
          // left edge instead of this generic label/value text (anchored to
          // the list's sidePadding) and the HP bar (anchored to the icon)
          // starting at two different x's. Still register the row (empty
          // label) so touch/selection keep working.
          row(local, "");
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
        // "Lv N" goes in the subtitle slot (its own line under the name,
        // spanning the full row width) rather than the value slot (which
        // sits to the name's right, competing with it for horizontal space)
        // - a value slot there was cutting off longer Pokemon names. Gender
        // is appended straight onto the label instead, right after the
        // name, matching Party/Battle's placement.
        char label[40];
        snprintf(label, sizeof(label), "%s %s",
                 record.nickname[0] == '\0' ? speciesName(record.speciesId) : record.nickname.data(),
                 genderAbbrev(record.gender));
        row(local, label);
        snprintf(values_[local].data(), values_[local].size(), "%s %u", tr(STR_POKEMON_LEVEL),
                 pokemon::levelForXp(record.totalXp));
        rows_[local].subtitle = values_[local].data();
        break;
      }
      // Unreachable in practice: isListScreen() excludes Screen::PcOrder (it
      // draws its own single-column button list via renderPcOrderButtons(),
      // which has its own copy of these same 3 labels - see the comment
      // there).
      case Screen::PcOrder:
        row(local, index == 0   ? tr(STR_POKEMON_CATCH_DATE)
                   : index == 1 ? tr(STR_POKEMON_NUMBER)
                                : tr(STR_POKEMON_ALPHABETICAL));
        break;
      // Unreachable in practice: isListScreen() excludes Screen::Bag (it
      // draws its own 2-column grid via renderBagGrid(), which has its own
      // copy of these same 4 labels - see the comment there).
      case Screen::Bag:
        row(local, index == 0   ? tr(STR_POKEMON_BAG_EVOLUTION)
                   : index == 1 ? tr(STR_POKEMON_BAG_MEDICINE)
                   : index == 2 ? tr(STR_POKEMON_BAG_BALLS)
                                : tr(STR_POKEMON_BAG_MACHINES));
        break;
      case Screen::BagEvolution: {
        const int slot = ownedSlotAt(static_cast<size_t>(index), snapshot_.state.itemCounts);
        if (slot < 0) break;
        const auto item = static_cast<pokemon::EvolutionItem>(slot + 1);
        char count[16];
        snprintf(count, sizeof(count), "× %u", snapshot_.state.itemCounts[slot]);
        row(local, itemName(item), count);
        break;
      }
      case Screen::BagBalls: {
        // Same fixed id/bagCounts indexing Screen::BattleBalls already uses
        // (ids 7-10 right after the 6 evolution stones) - this screen is
        // purely a read-only view of the same counts, no separate storage.
        const int slot =
            ownedSlotAt(static_cast<size_t>(index), std::span<const uint8_t>(snapshot_.state.bagCounts).first(4));
        if (slot < 0) break;
        const pokemon::ItemData* item =
            pokemon::itemData(static_cast<uint8_t>(pokemon::EVOLUTION_ITEM_COUNT + 1 + slot));
        char count[16];
        snprintf(count, sizeof(count), "× %u", snapshot_.state.bagCounts[slot]);
        row(local, item == nullptr ? "?" : item->name, count);
        break;
      }
      case Screen::BagMedicine:
      case Screen::BagMachine: {
        const bool machine = screen_ == Screen::BagMachine;
        const uint8_t itemId = bagItemIdAt(static_cast<size_t>(index), snapshot_.state.bagCounts,
                                           machine ? isMachineCategory : isMedicineCategory);
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
      // Unreachable in practice: isListScreen() excludes Screen::Battle
      // (it draws its own 2-column grid via renderBattleMenu(), which has
      // its own copy of this same label logic), so buildRows() is never
      // called with screen_ == Battle. Kept only so this switch stays
      // exhaustive over Screen's enumerators.
      case Screen::Battle: {
        const bool isGym = gymChallengeIndex_ != 0;
        const char* label;
        if (index == 0) {
          label = tr(STR_POKEMON_FIGHT);
        } else if (!isGym && index == 1) {
          label = tr(STR_POKEMON_BALL);
        } else if (index == (isGym ? 1 : 2)) {
          label = tr(STR_POKEMON_BAG);
        } else if (index == (isGym ? 2 : 3)) {
          label = tr(STR_POKEMON_SWITCH);
        } else {
          label = tr(STR_POKEMON_RUN);
        }
        row(local, label);
        break;
      }
      // Unreachable in practice: isListScreen() excludes Screen::BattleMoves
      // (it draws its own 2-column grid via renderBattleMoveMenu(), which
      // has its own copy of this same move/PP text). Kept only so this
      // switch stays exhaustive over Screen's enumerators.
      case Screen::BattleMoves: {
        const uint8_t moveId = battlePlayer_.moves[index].moveId;
        const pokemon::MoveData* move = pokemon::moveData(moveId);
        char value[16];
        snprintf(value, sizeof(value), "PP %u/%u", battlePlayer_.moves[index].currentPp,
                 move == nullptr ? 0 : move->pp);
        row(local, move == nullptr ? "?" : move->name, value);
        break;
      }
      case Screen::BattleSwitch: {
        const int slot = usablePartySlotAt(static_cast<size_t>(index));
        if (slot < 0) break;
        const pokemon::PokemonRecord& record = snapshot_.party[slot];
        // HP/status now show via the taller row's health strip
        // (showsPartyHealthRows()) instead of a plain "HP %u" value text -
        // the value slot shows Level instead, matching Party's row. Gender
        // is appended onto the label instead, right after the name.
        char label[40];
        snprintf(label, sizeof(label), "%s %s",
                 record.nickname[0] == '\0' ? speciesName(record.speciesId) : record.nickname.data(),
                 genderAbbrev(record.gender));
        char value[16];
        snprintf(value, sizeof(value), "%s %u", tr(STR_POKEMON_LEVEL), pokemon::levelForXp(record.totalXp));
        row(local, label, value);
        break;
      }
      case Screen::BattleBag: {
        const uint8_t itemId =
            bagItemIdAt(static_cast<size_t>(index), snapshot_.state.bagCounts, isBattleUsableCategory);
        const pokemon::ItemData* item = pokemon::itemData(itemId);
        const auto bagIndex = static_cast<size_t>(itemId - pokemon::EVOLUTION_ITEM_COUNT - 1U);
        char value[16];
        snprintf(value, sizeof(value), "× %u",
                 bagIndex < snapshot_.state.bagCounts.size() ? snapshot_.state.bagCounts[bagIndex] : 0);
        row(local, item == nullptr ? "?" : item->name, value);
        break;
      }
      case Screen::BattleBalls: {
        const int slot =
            ownedSlotAt(static_cast<size_t>(index), std::span<const uint8_t>(snapshot_.state.bagCounts).first(4));
        if (slot < 0) break;
        const pokemon::ItemData* item =
            pokemon::itemData(static_cast<uint8_t>(pokemon::EVOLUTION_ITEM_COUNT + 1 + slot));
        char value[16];
        snprintf(value, sizeof(value), "× %u", snapshot_.state.bagCounts[slot]);
        row(local, item == nullptr ? "?" : item->name, value);
        break;
      }
      case Screen::GymList: {
        const auto gymIndex = static_cast<uint8_t>(index + 1);
        const pokemon::GymData* gym = pokemon::gymData(gymIndex);
        char label[40];
        if (gymIndex <= 8U) {
          snprintf(label, sizeof(label), "%s", gym == nullptr ? "?" : gym->leaderName);
        } else if (gymIndex == pokemon::CHAMPION_GYM_INDEX) {
          snprintf(label, sizeof(label), "%s - %s", tr(STR_POKEMON_CHAMPION), gym == nullptr ? "?" : gym->leaderName);
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
                       screen_ == Screen::Pokedex || screen_ == Screen::BattleSwitch || screen_ == Screen::BagBalls ||
                       screen_ == Screen::BagMedicine || screen_ == Screen::BagMachine ||
                       screen_ == Screen::BattleBag || screen_ == Screen::BattleBalls || screen_ == Screen::Badges ||
                       screen_ == Screen::GymList;
  int top = listTop();
  rowHeight_ = rowHeightForScreen();
  // BattleBalls stays bottom-anchored, overlaid on the still-visible battle
  // HUD, because its row count is always small (at most 4) - it always fits
  // under the HUD. BattleBag (Stage 18) can list up to 17 items (every Medicine/
  // StatusCure/PPRestore id, regardless of how many the player owns), which
  // does NOT reliably fit there - forcing it into the same bottom-anchored
  // math pushed the list's top edge up over the HUD instead of scrolling
  // normally. So BattleBag deliberately uses the regular full-height
  // top-anchored list instead (see also renderFocused(), which
  // correspondingly does not draw the HUD behind it).
  // Screen::Battle/BattleMoves never reach here - isListScreen() excludes
  // both (they draw their own 2-column button grids, see
  // renderBattleMenu()/renderBattleMoveMenu()) - a single-column list of up
  // to 4 moves used to need 4 stacked rows tall enough to push into the
  // battle log box above it; the 2-column grid halves that to 2 rows.
  const bool bottomAnchored = screen_ == Screen::Event || screen_ == Screen::BattleBalls;
  if (bottomAnchored) top = renderer.getScreenHeight() - metrics.buttonHintsHeight - rowCount_ * rowHeight_ - 8;
  listBounds_ = Rect{8, top, renderer.getScreenWidth() - 16, rowCount_ * rowHeight_};
  // setContentMargin() insets from frame_.safeRect() (the screen already
  // shrunk by the device's top/bottom viewable margin), not from the raw
  // screen - see Screen::setContentMargin()/insetClamped() in FreeInkApp.h.
  // listBounds_ above is computed in raw-screen coordinates, so passing it
  // straight through double-applies the viewable margin (once here, once
  // inside safeRect()) and silently clips the last row. Subtract the same
  // margin back out here so the resulting content rect matches listBounds_
  // exactly.
  int viewableTop = 0, viewableRight = 0, viewableBottom = 0, viewableLeft = 0;
  renderer.getOrientedViewableTRBL(&viewableTop, &viewableRight, &viewableBottom, &viewableLeft);
  screen.setContentMargin(fui::Insets{
      static_cast<int16_t>(listBounds_.y - viewableTop), 8,
      static_cast<int16_t>(renderer.getScreenHeight() - listBounds_.y - listBounds_.height - viewableBottom), 8});
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
    // Touch-only devices (X4 Pro) have no physical Back button, so the
    // full-bleed card needs its own visible way out. loop() already checks
    // TouchHeaderBackButton::wasTapped() unconditionally every frame (its
    // hit-region doesn't depend on what's actually drawn), so this only
    // needs to draw *something* tappable there - reusing the exact same
    // header/icon rect keeps the drawn spot and the tappable spot from ever
    // drifting apart. A filled rounded backdrop keeps the chevron legible
    // over busy artwork instead of relying on the icon's own contrast.
    if (mappedInput.hasTouchHardware()) {
      const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInput);
      const auto backLayout = TouchHeaderBackButton::layout(header);
      constexpr int pad = 6;
      const int backdropX = backLayout.iconRect.x - pad;
      const int backdropY = backLayout.iconRect.y - pad;
      const int backdropW = backLayout.iconRect.width + 2 * pad;
      const int backdropH = backLayout.iconRect.height + 2 * pad;
      renderer.fillRoundedRect(backdropX, backdropY, backdropW, backdropH, backdropH / 2, Color::White);
      renderer.drawRoundedRect(backdropX, backdropY, backdropW, backdropH, 2, backdropH / 2, true);
      const int iconX = backLayout.iconRect.x + (backLayout.iconRect.width - TouchHeaderBackButton::ICON_SIZE) / 2;
      const int iconY = backLayout.iconRect.y + (backLayout.iconRect.height - TouchHeaderBackButton::ICON_SIZE) / 2;
      // Draw via the same fui target/bitmap path TouchHeaderBackButton::draw()
      // uses (not the lower-level GfxRenderer::drawIcon(), which renders this
      // particular icon rotated 90 degrees for reasons not worth chasing here).
      auto target = makeUiTarget(renderer);
      target.bitmap(
          fui::Rect{static_cast<int16_t>(iconX), static_cast<int16_t>(iconY),
                    static_cast<int16_t>(TouchHeaderBackButton::ICON_SIZE),
                    static_cast<int16_t>(TouchHeaderBackButton::ICON_SIZE)},
          fui::bitmapFromIcon(icon_back_32), fui::BitmapMode::Center);
    }
    return;
  }
  const int contentTop = metrics.topPadding + TouchHeaderBackButton::height(metrics, mappedInput) + 14;
  if (screen_ == Screen::Message) {
    // message_ is an arbitrary sentence (up to 96 chars, e.g. "None of your
    // Pokémon can battle right now!") - centered() alone only understands a
    // single already-embedded '\n', so a long line with none would just run
    // past the screen edge in portrait. Word-wrap it first.
    const int maxWidth = renderer.getScreenWidth() - 48;
    const auto lines = renderer.wrappedText(UI_12_FONT_ID, message_, maxWidth, 6, EpdFontFamily::BOLD);
    int messageLineY = contentTop + 100;
    for (const auto& line : lines) {
      centered(renderer, UI_12_FONT_ID, messageLineY, line.c_str(), EpdFontFamily::BOLD);
      messageLineY += 28;
    }
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
  // Bag category screens: item rows now hide anything owned in count 0
  // (Stage 19 follow-up "declutter"), so a category can legitimately end up
  // empty (e.g. a fresh save with no Medicine yet) - same empty-state
  // pattern as Screen::Pc above, rather than rendering a blank list.
  if ((screen_ == Screen::BagEvolution || screen_ == Screen::BagMedicine || screen_ == Screen::BagBalls ||
       screen_ == Screen::BagMachine) &&
      logicalCount() == 0) {
    centered(renderer, UI_12_FONT_ID, contentTop + 100, tr(STR_POKEMON_BAG_EMPTY), EpdFontFamily::BOLD);
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
    {
      const pokemon::BattleRecordEntry entry = service_.peekBattleMoves(record);
      const pokemon::BaseStats* baseStats = pokemon::baseStatsFor(record.speciesId);
      const uint16_t maxHp =
          baseStats == nullptr ? 1 : pokemon::battleMaxHp(baseStats->hp, pokemon::levelForXp(record.totalXp));
      renderer.drawText(UI_10_FONT_ID, textX, y, "HP", true, EpdFontFamily::BOLD);
      char hpText[16];
      snprintf(hpText, sizeof(hpText), "%u/%u", entry.currentHp, maxHp);
      const int hpTextW = renderer.getTextWidth(UI_10_FONT_ID, hpText);
      renderer.drawText(UI_10_FONT_ID, valueRight - hpTextW, y, hpText);
      const int barX = textX + renderer.getTextWidth(UI_10_FONT_ID, "HP", EpdFontFamily::BOLD) + 8;
      const int barRight = valueRight - hpTextW - 10;
      constexpr int barH = 10;
      if (barRight > barX) {
        renderer.drawRect(barX, y + 2, barRight - barX, barH, true);
        const int filled = maxHp == 0 ? 0 : (barRight - barX - 2) * std::min<uint16_t>(entry.currentHp, maxHp) / maxHp;
        if (filled > 0) renderer.fillRect(barX + 1, y + 3, filled, barH - 2, true);
      }
    }
    y += 26;
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
    // enough already (see the roadmap's Stage 7 note) that a 4-line block
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
  if (screen_ == Screen::Menu) {
    renderMenuGrid();
    return;
  }
  if (screen_ == Screen::Bag) {
    renderBagGrid();
    return;
  }
  if (screen_ == Screen::PcOrder) {
    renderPcOrderButtons();
    return;
  }
  if (screen_ == Screen::Battle || screen_ == Screen::BattleMoves || screen_ == Screen::BattleBalls) {
    renderBattleHud();
    if (screen_ == Screen::Battle) renderBattleMenu();
    if (screen_ == Screen::BattleMoves) renderBattleMoveMenu();
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

// Top Y of the 2-column command/move grid, anchored to the bottom of the
// screen exactly like the generic list's own bottomAnchored math
// (buildList()) - just with ceil(count/2) rows instead of `count` rows,
// since two commands/moves share each physical row.
//
// Screen::BattleMoves always reserves the full 2 rows (BATTLE_MOVE_SLOTS)
// here rather than however many moves this particular Pokemon actually
// knows: the rest of the battle HUD (opponent/player boxes, the log box)
// sizes itself off this same top, so a Pokemon with only 1-2 moves would
// otherwise reflow - and visually shift - the whole HUD every time FIGHT is
// opened, instead of just showing fewer buttons in an otherwise fixed-size
// grid.
int PokemonActivity::battleMenuTop() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int count = screen_ == Screen::BattleMoves ? static_cast<int>(pokemon::BATTLE_MOVE_SLOTS) : logicalCount();
  const int rows = (count + BATTLE_MENU_COLUMNS - 1) / BATTLE_MENU_COLUMNS;
  return renderer.getScreenHeight() - metrics.buttonHintsHeight - rows * BATTLE_MENU_ROW_HEIGHT - 8;
}

// Shared by renderBattleMenu()/renderBattleMoveMenu() (what gets drawn) and
// loop()'s touch hit-test (what gets tapped) - computed once here so the two
// can never drift apart.
Rect PokemonActivity::battleGridCellRect(int index) const {
  const int width = renderer.getScreenWidth();
  constexpr int margin = 8;
  constexpr int gap = 8;
  constexpr int buttonHeight = BATTLE_MENU_ROW_HEIGHT - 8;
  const int buttonWidth = (width - 2 * margin - gap * (BATTLE_MENU_COLUMNS - 1)) / BATTLE_MENU_COLUMNS;
  const int menuTop = battleMenuTop();
  const int row = index / BATTLE_MENU_COLUMNS;
  const int column = index % BATTLE_MENU_COLUMNS;
  const int x = margin + column * (buttonWidth + gap);
  const int y = menuTop + row * BATTLE_MENU_ROW_HEIGHT;
  return Rect{x, y, buttonWidth, buttonHeight};
}

// Shared top anchor for every top-anchored 2-column button grid on this
// activity (Menu/Bag/Pc) - unlike the Battle grid (battleMenuTop()), which
// anchors to the bottom to leave room for the HUD above it, these screens
// have nothing above the grid, so it just starts at the normal list top.
int PokemonActivity::buttonGridTop() const { return listTop(); }

// Shared by every renderXxxGrid() (what gets drawn) and loop()'s touch
// hit-test (what gets tapped) - computed once here so the two can never
// drift apart, same reasoning as battleGridCellRect(). `index` is always
// relative to the current page (0-based within whatever is on screen right
// now) - Menu/Bag never paginate, so this is just the global index for
// them.
Rect PokemonActivity::buttonGridCellRect(int index, int columns) const {
  const int width = renderer.getScreenWidth();
  constexpr int margin = 8;
  constexpr int gap = 8;
  constexpr int buttonHeight = MENU_GRID_ROW_HEIGHT - 8;
  const int buttonWidth = (width - 2 * margin - gap * (columns - 1)) / columns;
  const int top = buttonGridTop();
  const int row = index / columns;
  const int column = index % columns;
  const int x = margin + column * (buttonWidth + gap);
  const int y = top + row * MENU_GRID_ROW_HEIGHT;
  return Rect{x, y, buttonWidth, buttonHeight};
}

// One button cell's chrome (filled when selected, outlined otherwise) plus a
// single centered bold label.
void PokemonActivity::drawGridButton(const Rect& cell, bool selected, const char* label) {
  if (selected) {
    renderer.fillRoundedRect(cell.x, cell.y, cell.width, cell.height, 6, Color::Black);
  } else {
    renderer.drawRoundedRect(cell.x, cell.y, cell.width, cell.height, 2, 6, true);
  }
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, label, EpdFontFamily::BOLD);
  const int textX = cell.x + std::max(0, (cell.width - textWidth) / 2);
  const int textY = cell.y + std::max(0, (cell.height - renderer.getLineHeight(UI_12_FONT_ID)) / 2);
  renderer.drawText(UI_12_FONT_ID, textX, textY, label, !selected, EpdFontFamily::BOLD);
}

// The top-level Pokemon menu (Party/Pokedex/PC Box/PC Sort/Bag/Gym Battle/
// Badges/Settings) as a 2-column button grid instead of the generic
// single-column list - same shape/style as renderBattleMenu(), just
// top-anchored (buttonGridTop() == listTop()) since there's no HUD above it
// to leave room for. Bypasses buildList() entirely (see isListScreen());
// loop() has a matching grid-aware navigation branch.
void PokemonActivity::renderMenuGrid() {
  const int count = logicalCount();
  if (count <= 0) return;
  for (int index = 0; index < count; ++index) {
    const char* label = index == 0   ? tr(STR_POKEMON_PARTY)
                        : index == 1 ? tr(STR_POKEDEX)
                        : index == 2 ? tr(STR_POKEMON_PC_BOX)
                        : index == 3 ? tr(STR_POKEMON_PC_SORT)
                        : index == 4 ? tr(STR_POKEMON_BAG)
                        : index == 5 ? tr(STR_POKEMON_GYM_BATTLE)
                        : index == 6 ? tr(STR_POKEMON_BADGES)
                                     : tr(STR_POKEMON_SETTINGS);
    drawGridButton(buttonGridCellRect(index), index == selected_, label);
  }
}

// Bag's category picker (Evolution/Medicine/Balls/Machine) as the same
// 2-column button grid style, matching the user's follow-up request after
// the Menu grid landed. Always exactly 4 entries, one page - no pagination
// needed. Bypasses buildList() entirely (see isListScreen()); loop() has a
// matching grid-aware navigation branch.
void PokemonActivity::renderBagGrid() {
  const int count = logicalCount();
  if (count <= 0) return;
  for (int index = 0; index < count; ++index) {
    const char* label = index == 0   ? tr(STR_POKEMON_BAG_EVOLUTION)
                        : index == 1 ? tr(STR_POKEMON_BAG_MEDICINE)
                        : index == 2 ? tr(STR_POKEMON_BAG_BALLS)
                                     : tr(STR_POKEMON_BAG_MACHINES);
    drawGridButton(buttonGridCellRect(index), index == selected_, label);
  }
}

// PC Box's sort picker (Catch Date/Number/Alphabetical) as the same button
// style, but a single column - only 3 entries, and full-width buttons read
// better than a 2-column grid for this short a list. Bypasses buildList()
// entirely (see isListScreen()); loop() has a matching navigation branch.
void PokemonActivity::renderPcOrderButtons() {
  const int count = logicalCount();
  if (count <= 0) return;
  for (int index = 0; index < count; ++index) {
    const char* label = index == 0   ? tr(STR_POKEMON_CATCH_DATE)
                        : index == 1 ? tr(STR_POKEMON_NUMBER)
                                     : tr(STR_POKEMON_ALPHABETICAL);
    drawGridButton(buttonGridCellRect(index, 1), index == selected_, label);
  }
}

// Screen::Battle's FIGHT/BALL/BAG/SWITCH/RUN menu as a 2-column button grid
// instead of the generic single-column list - halves the vertical space the
// menu needs (5 commands for a wild encounter used to mean 5 stacked rows,
// which pushed into the battle HUD above it). Bypasses buildList() entirely
// (see isListScreen()); loop() has a matching grid-aware navigation branch.
void PokemonActivity::renderBattleMenu() {
  const int count = logicalCount();
  if (count <= 0) return;
  const bool isGym = gymChallengeIndex_ != 0;

  for (int index = 0; index < count; ++index) {
    const Rect cell = battleGridCellRect(index);
    const int x = cell.x;
    const int y = cell.y;
    const int buttonWidth = cell.width;
    const int buttonHeight = cell.height;

    const char* label;
    if (index == 0) {
      label = tr(STR_POKEMON_FIGHT);
    } else if (!isGym && index == 1) {
      label = tr(STR_POKEMON_BALL);
    } else if (index == (isGym ? 1 : 2)) {
      label = tr(STR_POKEMON_BAG);
    } else if (index == (isGym ? 2 : 3)) {
      label = tr(STR_POKEMON_SWITCH);
    } else {
      label = tr(STR_POKEMON_RUN);
    }

    const bool selected = index == selected_;
    if (selected) {
      renderer.fillRoundedRect(x, y, buttonWidth, buttonHeight, 6, Color::Black);
    } else {
      renderer.drawRoundedRect(x, y, buttonWidth, buttonHeight, 2, 6, true);
    }
    const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, label, EpdFontFamily::BOLD);
    const int textX = x + std::max(0, (buttonWidth - textWidth) / 2);
    const int textY = y + std::max(0, (buttonHeight - renderer.getLineHeight(UI_12_FONT_ID)) / 2);
    renderer.drawText(UI_12_FONT_ID, textX, textY, label, !selected, EpdFontFamily::BOLD);
  }
}

// Screen::BattleMoves' up-to-4 moves as the same 2-column button grid shape
// as renderBattleMenu() (reuses battleMenuTop() - logicalCount() already
// returns battlePlayerMoveCount() when this screen is active, so the "N
// items, 2-column grid" math is identical). A single-column list of 4 moves
// used to need 4 stacked rows tall enough to push up into the still-visible
// battle log box; 2 columns halves that to 2 rows. Name and PP share one
// row, vertically centered in the button (a stacked two-line layout pushed
// PP past the button's bottom edge) - name in bold on the left (truncated to
// leave room for PP, so long names like Solar Beam/Sky Attack never collide
// with it), PP in the plain (non-bold) weight on the right so it reads as
// secondary - there's no smaller font available on this device to shrink it
// further (only UI_10_FONT_ID/UI_12_FONT_ID exist).
void PokemonActivity::renderBattleMoveMenu() {
  const int count = battlePlayerMoveCount();
  if (count <= 0) return;
  constexpr int textPad = 10;
  constexpr int nameToPpGap = 8;

  for (int index = 0; index < count; ++index) {
    const Rect cell = battleGridCellRect(index);
    const int x = cell.x;
    const int y = cell.y;
    const int buttonWidth = cell.width;
    const int buttonHeight = cell.height;

    const uint8_t moveId = battlePlayer_.moves[index].moveId;
    const pokemon::MoveData* move = pokemon::moveData(moveId);
    char pp[16];
    snprintf(pp, sizeof(pp), "%u/%u", battlePlayer_.moves[index].currentPp, move == nullptr ? 0 : move->pp);
    const int ppWidth = renderer.getTextWidth(UI_10_FONT_ID, pp);
    const int nameMaxWidth = std::max(0, buttonWidth - 2 * textPad - nameToPpGap - ppWidth);
    const std::string name =
        renderer.truncatedText(UI_10_FONT_ID, move == nullptr ? "?" : move->name, nameMaxWidth, EpdFontFamily::BOLD);

    const bool selected = index == selected_;
    if (selected) {
      renderer.fillRoundedRect(x, y, buttonWidth, buttonHeight, 6, Color::Black);
    } else {
      renderer.drawRoundedRect(x, y, buttonWidth, buttonHeight, 2, 6, true);
    }
    const bool black = !selected;
    const int textY = y + std::max(0, (buttonHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2);
    renderer.drawText(UI_10_FONT_ID, x + textPad, textY, name.c_str(), black, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, x + buttonWidth - textPad - ppWidth, textY, pp, black);
  }
}

// Pokemon Red's battlefield reads as a diagonal: the opponent's compact HP
// box sits top-left with its sprite floating top-right, the player's sprite
// sits bottom-left with its own compact HP box bottom-right, and a bordered
// message box holds the turn-by-turn log above the FIGHT/BALL/BAG/SWITCH/RUN
// grid (renderBattleMenu()). A row of dots above each box (filled = still
// able to battle, crossed-out circle = fainted) shows how many Pokemon
// remain on each side, mirroring Red's row of Poke Balls above a trainer's
// HP box.
void PokemonActivity::renderBattleHud() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + TouchHeaderBackButton::height(metrics, mappedInput) + 14;
  const int width = renderer.getScreenWidth();

  const int hudTop = contentTop;
  // Screen::Battle/BattleMoves compute their own menu top (battleMenuTop(),
  // see there) - neither is a list screen anymore, so listBounds_ is never
  // set for them; logicalCount() already returns the right count for
  // whichever of the two is active (command count vs. move count), so
  // battleMenuTop()'s generic "N items, 2-column grid" math applies to both.
  // BattleBalls is still an ordinary bottom-anchored list (buildList() sets
  // listBounds_ for it using its own item count).
  const int hudBottom =
      (screen_ == Screen::Battle || screen_ == Screen::BattleMoves ? battleMenuTop() : listBounds_.y) - 12;
  const int available = hudBottom - hudTop;

  // The classic two-row diagonal layout below (opponent zone stacked above
  // player zone, each a fixed-size sprite + HP panel) needs roughly
  // 2*112px for the two zones plus a legible message box - comfortable in
  // portrait, where `available` is 400+px. Neither X3 nor X4 Pro comes
  // anywhere close to that in LANDSCAPE: screen height drops to 528/480
  // respectively, and after the header and the FIGHT/BAG/SWITCH/RUN menu
  // eat into it, both land around ~170px available (measured directly:
  // X4 Pro simulator gives hudTop=96 hudBottom=268, i.e. available=172) -
  // not enough room for even ONE stacked pair of full-size zones, let alone
  // two. Below this threshold, use a compact side-by-side layout instead:
  // opponent's HP panel in the left half, player's in the right half,
  // sharing a single row with smaller sprites/panels/message box, rather
  // than silently overlapping (confirmed via an actual landscape
  // screenshot before this fix - the two panels and the message box all
  // drew on top of each other).
  constexpr int classicZoneContentHeight = 112;  // spriteH(90) vs dotRowHeight(20)+panelHeight(92)
  constexpr int classicMinRequired = 2 * classicZoneContentHeight + 12 /* messageGap */ + 70 /* min message */ + 16;
  const bool compact = available < classicMinRequired;

  const int spriteW = compact ? 64 : 120;  // fixed-AR hero-art asset (4:3); GfxRenderer downscales to fit, never upscales
  const int spriteH = compact ? 48 : 90;
  const int sideMargin = compact ? 12 : 24;
  const int dotSize = compact ? 8 : 12;
  const int dotGap = compact ? 4 : 6;
  const int dotRowHeight = dotSize + (compact ? 4 : 8);
  // Compact panels sit between their side's sprite (touching the outer
  // screen edge) and the screen's horizontal center, so both panels/sprites
  // fit side by side within their own half - see the position math below.
  constexpr int compactSpriteGap = 8;
  constexpr int compactCenterGap = 8;
  const int halfWidth = width / 2;
  const int panelWidth = compact ? (halfWidth - sideMargin - spriteW - compactSpriteGap - compactCenterGap) : 220;
  // Compact mode allocates its own row height directly from `available`
  // (clamped to a legible range) instead of the classic fixed 92px, then
  // gives whatever's left over to the message box - avoids the circular
  // "panel needs message's leftover, message needs panel's leftover" trap.
  const int compactRowHeight = std::clamp(static_cast<int>(available * 0.6), 60, 110);
  const int panelHeight = compact ? std::max(40, compactRowHeight - dotRowHeight) : 92;
  constexpr int spriteGapMin = 16;
  constexpr int messageGap = 12;
  const int messageHeight = compact ? std::max(36, available - compactRowHeight - messageGap) : 160;
  const int barHeight = compact ? 10 : 12;

  // Compact box: name/nickname + level on top; below that, the HP bar with
  // its "cur/max" text right after it (and status past that) vertically
  // centered in the remaining box height.
  const auto drawPanel = [&](const pokemon::BattleCombatant& combatant, const char* nameText, const int panelX,
                             const int panelY) {
    renderer.drawRoundedRect(panelX, panelY, panelWidth, panelHeight, 2, 6, true);
    char levelLine[16];
    snprintf(levelLine, sizeof(levelLine), "%s%u", tr(STR_POKEMON_LEVEL), combatant.level);
    const int levelW = renderer.getTextWidth(UI_10_FONT_ID, levelLine, EpdFontFamily::REGULAR);

    const int nameY = panelY + 8;
    char nameWithGender[40];
    snprintf(nameWithGender, sizeof(nameWithGender), "%s %s", nameText, genderAbbrev(combatant.gender));
    renderer.drawText(UI_10_FONT_ID, panelX + 8, nameY, nameWithGender, true, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, panelX + panelWidth - 8 - levelW, nameY, levelLine);

    char hpText[16];
    snprintf(hpText, sizeof(hpText), "%u/%u", combatant.currentHp, combatant.maxHp);
    const int hpTextW = renderer.getTextWidth(UI_10_FONT_ID, hpText);
    const char* status = combatant.status == pokemon::Ailment::None ? nullptr : statusAbbrev(combatant.status);
    const int statusW = status == nullptr ? 0 : renderer.getTextWidth(UI_10_FONT_ID, status, EpdFontFamily::BOLD);

    // Vertically center the HP row in the space below the name line.
    const int nameRowBottom = nameY + 20;
    const int rowY = nameRowBottom + std::max(0, (panelY + panelHeight - 8 - nameRowBottom - barHeight) / 2);
    const char* hpLabel = "HP";
    renderer.drawText(UI_10_FONT_ID, panelX + 8, rowY - 1, hpLabel, true, EpdFontFamily::BOLD);
    const int barX = panelX + 8 + renderer.getTextWidth(UI_10_FONT_ID, hpLabel, EpdFontFamily::BOLD) + 6;
    const int barRight = panelX + panelWidth - 8 - (status == nullptr ? 0 : statusW + 8) - hpTextW - 6;
    renderer.drawRect(barX, rowY, barRight - barX, barHeight, true);
    const uint16_t maxHp = std::max<uint16_t>(1, combatant.maxHp);
    const int filled = combatant.maxHp == 0 ? 0 : (barRight - barX - 2) * combatant.currentHp / maxHp;
    if (filled > 0) renderer.fillRect(barX + 1, rowY + 1, filled, barHeight - 2, true);
    renderer.drawText(UI_10_FONT_ID, barRight + 6, rowY - 1, hpText);
    if (status != nullptr) {
      renderer.drawText(UI_10_FONT_ID, panelX + panelWidth - 8 - statusW, rowY - 1, status, true, EpdFontFamily::BOLD);
    }
  };

  // aliveMask bit i = 1 when party/team member i can still battle. rightEdge
  // is the row's right edge (dots grow leftward) since both dot rows sit
  // above their side's box, and both boxes are right-of-center in this
  // layout (opponent panel is the exception - see the call site below).
  const auto drawDots = [&](const int count, const uint32_t aliveMask, const int rowY, const int rightEdge) {
    if (count <= 0) return;
    const int totalW = count * dotSize + (count - 1) * dotGap;
    int x = rightEdge - totalW;
    for (int i = 0; i < count; ++i) {
      const bool alive = ((aliveMask >> i) & 1U) != 0;
      if (alive) {
        renderer.fillRoundedRect(x, rowY, dotSize, dotSize, dotSize / 2, Color::Black);
      } else {
        renderer.drawRoundedRect(x, rowY, dotSize, dotSize, 1, dotSize / 2, true);
        renderer.drawLine(x + 2, rowY + 2, x + dotSize - 2, rowY + dotSize - 2, 1, true);
        renderer.drawLine(x + 2, rowY + dotSize - 2, x + dotSize - 2, rowY + 2, 1, true);
      }
      x += dotSize + dotGap;
    }
  };

  int opponentCount = 1;
  uint32_t opponentAliveMask = battleOpponent_.currentHp > 0 ? 1U : 0U;
  if (gymChallengeIndex_ != 0) {
    const auto team = pokemon::gymTeamFor(gymChallengeIndex_);
    opponentCount = static_cast<int>(team.size());
    opponentAliveMask = 0;
    for (int i = 0; i < opponentCount; ++i) {
      bool alive;
      if (i < static_cast<int>(gymChallengeTeamProgress_)) {
        alive = false;  // already defeated
      } else if (i == static_cast<int>(gymChallengeTeamProgress_)) {
        alive = battleOpponent_.currentHp > 0;  // currently on the field
      } else {
        alive = true;  // not sent out yet
      }
      if (alive) opponentAliveMask |= (1U << i);
    }
  }
  uint32_t playerAliveMask = 0;
  for (int i = 0; i < snapshot_.partyCount; ++i) {
    bool alive;
    if (i == battlePartySlot_) {
      alive = battlePlayer_.currentHp > 0;  // the live combatant, not the (possibly stale) stored entry
    } else {
      alive = service_.peekBattleMoves(snapshot_.party[i]).currentHp > 0;
    }
    if (alive) playerAliveMask |= (1U << i);
  }

  const bool playerHasRecord = battlePartySlot_ >= 0 && battlePartySlot_ < snapshot_.partyCount;
  const char* playerNickname = playerHasRecord ? snapshot_.party[battlePartySlot_].nickname.data() : "";
  const char* playerName = playerNickname[0] == '\0' ? speciesName(battlePlayer_.speciesId) : playerNickname;

  int messageY;
  if (compact) {
    // Single row, opponent's panel+sprite in the left half and player's in
    // the right half - each sprite touches its half's outer screen edge, its
    // panel fills the rest of that half toward the center gap (see
    // panelWidth's formula above).
    const int rowTop = hudTop;
    const int oppSpriteX = sideMargin;
    const int oppPanelX = sideMargin + spriteW + compactSpriteGap;
    const int spriteY = rowTop + dotRowHeight + std::max(0, (panelHeight - spriteH) / 2);
    drawDots(opponentCount, opponentAliveMask, rowTop, oppPanelX + panelWidth);
    drawPanel(battleOpponent_, speciesName(battleOpponent_.speciesId), oppPanelX, rowTop + dotRowHeight);
    pokemon::drawPokemonSpeciesArt(renderer, battleOpponent_.speciesId, true,
                                   Rect{oppSpriteX, spriteY, spriteW, spriteH});

    const int playerPanelX = halfWidth + compactCenterGap;
    const int playerSpriteX = width - sideMargin - spriteW;
    drawDots(snapshot_.partyCount, playerAliveMask, rowTop, playerPanelX + panelWidth);
    drawPanel(battlePlayer_, playerName, playerPanelX, rowTop + dotRowHeight);
    pokemon::drawPokemonSpeciesBackArt(renderer, battlePlayer_.speciesId, Rect{playerSpriteX, spriteY, spriteW, spriteH});
    messageY = hudBottom - messageHeight;
  } else {
    // Each zone's height is whichever is taller: the fixed-size sprite, or
    // the dot row + HP panel stacked above it - the panel can outgrow the
    // sprite once it carries a name line again, so this can't assume the
    // sprite wins.
    const int zoneContentHeight = std::max(spriteH, dotRowHeight + panelHeight);

    // The message box is a fixed height pinned to the bottom instead of
    // soaking up whatever was left over, so the gap between the two zones
    // absorbs the freed space instead - keeping the battlefield (sprites +
    // HP boxes) the biggest thing on screen rather than a mostly-empty text
    // box.
    messageY = hudBottom - messageHeight;
    const int zoneGap = std::max(spriteGapMin, messageY - messageGap - hudTop - 2 * zoneContentHeight);

    const int opponentSpriteX = width - sideMargin - spriteW;
    const int opponentSpriteY = hudTop;
    const int opponentPanelX = sideMargin;
    drawDots(opponentCount, opponentAliveMask, hudTop, opponentPanelX + panelWidth);
    drawPanel(battleOpponent_, speciesName(battleOpponent_.speciesId), opponentPanelX, hudTop + dotRowHeight);
    pokemon::drawPokemonSpeciesArt(renderer, battleOpponent_.speciesId, true,
                                   Rect{opponentSpriteX, opponentSpriteY, spriteW, spriteH});

    const int playerSpriteX = sideMargin;
    const int playerZoneTop = hudTop + zoneContentHeight + zoneGap;
    const int playerSpriteY = playerZoneTop;
    const int playerPanelX = width - sideMargin - panelWidth;
    drawDots(snapshot_.partyCount, playerAliveMask, playerZoneTop, playerPanelX + panelWidth);
    drawPanel(battlePlayer_, playerName, playerPanelX, playerZoneTop + dotRowHeight);
    pokemon::drawPokemonSpeciesBackArt(renderer, battlePlayer_.speciesId,
                                      Rect{playerSpriteX, playerSpriteY, spriteW, spriteH});
  }

  if (messageY < hudTop) return;  // shouldn't happen at any supported screen size, but never draw a negative-size box
  const int messageX = sideMargin;
  const int messageW = width - 2 * sideMargin;
  renderer.drawRoundedRect(messageX, messageY, messageW, messageHeight, 2, 8, true);
  if (battleLog_[0] == '\0') return;
  const int textX = messageX + 16;
  const int textMaxWidth = messageW - 32;
  // battleLog_ is at most 2 logical lines (player action + opponent action,
  // joined by a single '\n' - see buildBattleLog() and every other
  // snprintf(battleLog_, ...) site). Each one word-wraps independently
  // instead of being truncated with an ellipsis, so a line that's merely
  // too WIDE reflows onto a second line rather than losing its tail; the
  // combined line budget is still capped to what messageHeight can hold, so
  // the box itself never overflows.
  const int textTopPad = compact ? 6 : 18;
  const int textBottomPad = compact ? 4 : 12;
  const int lineSpacing = compact ? 20 : 26;
  const int maxLines = std::max(1, (messageHeight - textTopPad - textBottomPad) / lineSpacing);
  std::vector<std::string> lines;
  const char* segmentStart = battleLog_;
  while (*segmentStart != '\0' && static_cast<int>(lines.size()) < maxLines) {
    const char* segmentEnd = strchr(segmentStart, '\n');
    const size_t segmentLen =
        segmentEnd != nullptr ? static_cast<size_t>(segmentEnd - segmentStart) : strlen(segmentStart);
    char segment[96];
    snprintf(segment, sizeof(segment), "%.*s", static_cast<int>(segmentLen), segmentStart);
    const int remainingLines = maxLines - static_cast<int>(lines.size());
    for (auto& wrapped : renderer.wrappedText(UI_10_FONT_ID, segment, textMaxWidth, remainingLines)) {
      if (static_cast<int>(lines.size()) >= maxLines) break;
      lines.push_back(std::move(wrapped));
    }
    if (segmentEnd == nullptr) break;
    segmentStart = segmentEnd + 1;
  }
  int lineY = messageY + textTopPad;
  for (const auto& line : lines) {
    renderer.drawText(UI_10_FONT_ID, textX, lineY, line.c_str());
    lineY += lineSpacing;
  }
}

void PokemonActivity::renderRowArt() {
  const bool artRows = screen_ == Screen::Starter || screen_ == Screen::Party || screen_ == Screen::Move ||
                       screen_ == Screen::Pc || screen_ == Screen::BagEvolution || screen_ == Screen::ItemTarget ||
                       screen_ == Screen::Pokedex || screen_ == Screen::BattleSwitch || screen_ == Screen::BagBalls ||
                       screen_ == Screen::BagMedicine || screen_ == Screen::BagMachine ||
                       screen_ == Screen::BattleBag || screen_ == Screen::BattleBalls || screen_ == Screen::Badges ||
                       screen_ == Screen::GymList;
  if (!artRows) return;
  const int start = pageStart();
  for (int local = 0; local < rowCount_; ++local) {
    const int rowY = listBounds_.y + local * rowHeight_;
    uint16_t speciesId = 0;
    // BattleSwitch skips the active combatant and any fainted party member,
    // so its row index doesn't line up with a plain party slot the way
    // Party/Move/ItemTarget's does - usablePartySlotAt() maps it.
    const int battleSwitchSlot =
        screen_ == Screen::BattleSwitch ? usablePartySlotAt(static_cast<size_t>(start + local)) : -1;
    if (screen_ == Screen::Starter)
      speciesId = STARTERS[start + local];
    else if ((screen_ == Screen::Party || screen_ == Screen::Move || screen_ == Screen::ItemTarget) &&
             start + local < snapshot_.partyCount)
      speciesId = snapshot_.party[start + local].speciesId;
    else if (screen_ == Screen::BattleSwitch && battleSwitchSlot >= 0)
      speciesId = snapshot_.party[battleSwitchSlot].speciesId;
    else if (screen_ == Screen::Pc && local < static_cast<int>(pcCount_))
      speciesId = pcPage_[local].speciesId;
    else if (screen_ == Screen::Pokedex &&
             pokemon::isSpeciesMarked(snapshot_.state.seenSpecies, static_cast<uint16_t>(start + local + 1))) {
      speciesId = static_cast<uint16_t>(start + local + 1);
    }
    const int evolutionSlot = screen_ == Screen::BagEvolution
                                  ? ownedSlotAt(static_cast<size_t>(start + local), snapshot_.state.itemCounts)
                                  : -1;
    // Same row->item-id mapping buildRows() uses for these screens - see the
    // matching cases there (BagBalls/BattleBalls share the fixed 4-slot ball
    // range; BagMedicine/BagMachine/BattleBag walk the full item table by
    // category via bagItemIdAt()).
    uint8_t bagItemId = 0;
    if (screen_ == Screen::BagBalls || screen_ == Screen::BattleBalls) {
      const int slot = ownedSlotAt(static_cast<size_t>(start + local),
                                   std::span<const uint8_t>(snapshot_.state.bagCounts).first(4));
      if (slot >= 0) bagItemId = static_cast<uint8_t>(pokemon::EVOLUTION_ITEM_COUNT + 1 + slot);
    } else if (screen_ == Screen::BagMedicine) {
      bagItemId = bagItemIdAt(static_cast<size_t>(start + local), snapshot_.state.bagCounts, isMedicineCategory);
    } else if (screen_ == Screen::BagMachine) {
      bagItemId = bagItemIdAt(static_cast<size_t>(start + local), snapshot_.state.bagCounts, isMachineCategory);
    } else if (screen_ == Screen::BattleBag) {
      bagItemId = bagItemIdAt(static_cast<size_t>(start + local), snapshot_.state.bagCounts, isBattleUsableCategory);
    }
    if (screen_ == Screen::BagEvolution && evolutionSlot >= 0) {
      // No icon assets exist for TM/HM/potions/etc - only the 6 evolution
      // stones (BagEvolution's rows) have art to draw here. Zero-count
      // stones are filtered from the list (Stage 19 follow-up), so the row
      // index isn't the stone slot directly - map it back first.
      constexpr int itemSize = 32;
      pokemon::drawPokemonItemArt(renderer, static_cast<pokemon::EvolutionItem>(evolutionSlot + 1), false,
                                  Rect{listBounds_.x + ROW_ICON_X + pokemon::pokemonCenteredOffset(80, itemSize),
                                       rowY + pokemon::pokemonCenteredOffset(rowHeight_, itemSize), itemSize, itemSize},
                                  false);
    } else if (bagItemId != 0) {
      constexpr int itemSize = 32;
      pokemon::drawPokemonBagItemArt(
          renderer, bagItemId,
          Rect{listBounds_.x + ROW_ICON_X + pokemon::pokemonCenteredOffset(80, itemSize),
               rowY + pokemon::pokemonCenteredOffset(rowHeight_, itemSize), itemSize, itemSize});
    } else if (screen_ == Screen::Badges) {
      const auto gymIndex = static_cast<uint8_t>(start + local + 1);
      constexpr int badgeSize = 32;
      pokemon::drawPokemonBadgeArt(
          renderer, gymIndex,
          Rect{listBounds_.x + ROW_ICON_X + pokemon::pokemonCenteredOffset(80, badgeSize),
               rowY + pokemon::pokemonCenteredOffset(rowHeight_, badgeSize), badgeSize, badgeSize});
    } else if (screen_ == Screen::GymList) {
      const auto gymIndex = static_cast<uint8_t>(start + local + 1);
      constexpr int trainerSize = 32;
      pokemon::drawPokemonTrainerArt(
          renderer, gymIndex,
          Rect{listBounds_.x + ROW_ICON_X + pokemon::pokemonCenteredOffset(80, trainerSize),
               rowY + pokemon::pokemonCenteredOffset(rowHeight_, trainerSize), trainerSize, trainerSize});
    } else if (speciesId != 0) {
      // The 40x30 menu files are intentionally native-sized and GfxRenderer
      // does not upscale. Use the same approved icon's 120x90 presentation
      // copy so it can be reduced cleanly into the row instead of appearing
      // as a tiny 40x30 mark on the X3 panel. Vertically centered in the row
      // (equals the old hardcoded +2 for a plain 64px row; keeps the icon
      // centered against the two-line text block on the taller 96px health
      // rows too instead of hugging the top).
      constexpr int speciesIconH = 60;
      pokemon::drawPokemonSpeciesArt(
          renderer, speciesId, true,
          Rect{listBounds_.x + ROW_ICON_X, rowY + pokemon::pokemonCenteredOffset(rowHeight_, speciesIconH), 80,
               speciesIconH});
    }
    if (showsPartyHealthRows()) {
      if ((screen_ == Screen::Party) && start + local < snapshot_.partyCount) {
        renderPartyRowHealth(rowY, snapshot_.party[start + local], true);
      } else if (screen_ == Screen::BattleSwitch && battleSwitchSlot >= 0) {
        // BattleSwitch keeps its own name/level/gender text via the generic
        // list widget (a different buildRows() case than Party/ItemTarget) -
        // only the HP bar/status strip is custom-drawn here, so the name
        // line must not be drawn a second time.
        renderPartyRowHealth(rowY, snapshot_.party[battleSwitchSlot], false);
      } else if (screen_ == Screen::ItemTarget && start + local < snapshot_.partyCount) {
        renderPartyRowHealth(rowY, snapshot_.party[start + local], true);
      }
    }
  }
}

// Drawn in the taller row (see rowHeightForScreen()) for Screen::Party and,
// since Stage 18, Screen::ItemTarget when picking who receives a Medicine item
// (showsPartyHealthRows()) - a self-contained two-line block to the right of
// the row's icon: name/level/gender on top (`drawNameLine`), HP bar/text/
// status underneath, both starting at the same left edge (textX). Name/
// level/gender used to be the generic list widget's own text (anchored to
// its own sidePadding) while the HP bar was anchored to the icon instead - two
// different x's, which is what actually read as messy, not the two-line idea
// itself. BattleSwitch passes drawNameLine=false and keeps its name/level/
// gender text from the generic list widget (a different buildRows() case,
// its own value string), so only the bar/status strip in the row's bottom
// area is drawn here for it, exactly as before. peekBattleMoves() is
// read-only (never creates or writes a battle-store entry), matching every
// other read-only HP peek in this file (Summary, usablePartySlotAt()).
void PokemonActivity::renderPartyRowHealth(const int rowY, const pokemon::PokemonRecord& record,
                                           const bool drawNameLine) {
  const pokemon::BattleRecordEntry entry = service_.peekBattleMoves(record);
  const pokemon::BaseStats* stats = pokemon::baseStatsFor(record.speciesId);
  const uint16_t maxHp = stats == nullptr ? 1 : pokemon::battleMaxHp(stats->hp, pokemon::levelForXp(record.totalXp));

  // Matches the generic list widget's own sidePadding for artwork rows
  // (pokemonListPresentation()) - the icon sits at ROW_ICON_X and this
  // starts right after it, both clearing the selection triangle now reserved
  // at the row's own left edge (markerInset=4) instead of squeezed in
  // between icon and text.
  const int textX = listBounds_.x + 112;
  const int textRight = listBounds_.x + listBounds_.width - 8;
  constexpr int barH = 8;

  int barY;
  int hpTextY;
  int statusY;
  int barX;
  int barW;
  if (drawNameLine) {
    // A bar that stretched to fill the whole remaining line read as too
    // long/heavy for what is otherwise a compact two-line row - cap it at a
    // reasonable width instead of always maximizing it, while still letting
    // it shrink further on a narrower screen.
    constexpr int maxBarW = 130;
    constexpr int hpTextReserve = 56;
    constexpr int statusReserve = 44;
    barW = std::min(maxBarW, std::max(40, textRight - textX - hpTextReserve - statusReserve));
    const int lineHeight1 = renderer.getLineHeight(UI_12_FONT_ID);
    const int lineHeight2 = renderer.getLineHeight(UI_10_FONT_ID);
    constexpr int lineGap = 8;
    const int blockTop = rowY + pokemon::pokemonCenteredOffset(rowHeight_, lineHeight1 + lineGap + lineHeight2);
    const int line2Top = blockTop + lineHeight1 + lineGap;

    char meta[16];
    snprintf(meta, sizeof(meta), "%s %u", tr(STR_POKEMON_LEVEL), pokemon::levelForXp(record.totalXp));
    const int metaWidth = renderer.getTextWidth(UI_12_FONT_ID, meta);
    const char* gender = genderAbbrev(record.gender);
    const int genderWidth = gender[0] == '\0' ? 0 : renderer.getTextWidth(UI_12_FONT_ID, gender) + 4;
    const int nameMaxWidth = std::max(0, textRight - textX - metaWidth - genderWidth - 10);
    const std::string name = renderer.truncatedText(
        UI_12_FONT_ID, record.nickname[0] == '\0' ? speciesName(record.speciesId) : record.nickname.data(),
        nameMaxWidth, EpdFontFamily::BOLD);
    const int nameWidth = renderer.getTextWidth(UI_12_FONT_ID, name.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, textX, blockTop, name.c_str(), true, EpdFontFamily::BOLD);
    if (gender[0] != '\0') renderer.drawText(UI_12_FONT_ID, textX + nameWidth + 4, blockTop, gender);
    renderer.drawText(UI_12_FONT_ID, textRight - metaWidth, blockTop, meta);

    barX = textX;
    barY = line2Top + std::max(0, (lineHeight2 - barH) / 2);
    hpTextY = line2Top;
    statusY = line2Top;
  } else {
    // BattleSwitch (the only caller with drawNameLine=false): the generic
    // list widget already draws this row's own name/level/gender text
    // starting at x=112 (pokemonListPresentation()'s artwork sidePadding),
    // clearing the 80px-wide species icon at ROW_ICON_X - this HP bar/status
    // strip sits on the row's second line, so it needs the same left edge,
    // not the icon's. Reusing ROW_ICON_X here drew the bar directly over the
    // icon's bottom edge.
    barX = listBounds_.x + 112;
    barY = rowY + rowHeight_ - 22;
    hpTextY = barY - 3;
    statusY = barY - 3;
    barW = 96;
  }

  renderer.drawRect(barX, barY, barW, barH, true);
  const int filled = maxHp == 0 ? 0 : (barW - 2) * std::min<uint16_t>(entry.currentHp, maxHp) / maxHp;
  if (filled > 0) renderer.fillRect(barX + 1, barY + 1, filled, barH - 2, true);

  char hpText[16];
  snprintf(hpText, sizeof(hpText), "%u/%u", entry.currentHp, maxHp);
  renderer.drawText(UI_10_FONT_ID, barX + barW + 8, hpTextY, hpText);

  if (entry.status != pokemon::Ailment::None) {
    const char* status = statusAbbrev(entry.status);
    const int statusRight = drawNameLine ? textRight : listBounds_.x + listBounds_.width - 8;
    renderer.drawText(UI_10_FONT_ID, statusRight - renderer.getTextWidth(UI_10_FONT_ID, status, EpdFontFamily::BOLD),
                      statusY, status, true, EpdFontFamily::BOLD);
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
  else if (screen_ == Screen::BagBalls)
    title = tr(STR_POKEMON_BAG_BALLS);
  else if (screen_ == Screen::BagMachine)
    title = tr(STR_POKEMON_BAG_MACHINES);
  else if (screen_ == Screen::Pokedex)
    title = tr(STR_POKEDEX);
  else if (screen_ == Screen::Summary || screen_ == Screen::Actions)
    title = tr(STR_POKEMON_SUMMARY);
  else if (screen_ == Screen::Moveset || screen_ == Screen::MovesetPick || screen_ == Screen::TmReplaceSlot)
    title = tr(STR_POKEMON_MOVES);
  else if (screen_ == Screen::BattleBag)
    title = tr(STR_POKEMON_BAG);
  else if (screen_ == Screen::BattleSwitch)
    title = tr(STR_POKEMON_SWITCH);
  else if (screen_ == Screen::GymList)
    title = tr(STR_POKEMON_GYM_BATTLE);
  else if (screen_ == Screen::Badges)
    title = tr(STR_POKEMON_BADGES);
  else if (screen_ == Screen::Settings || screen_ == Screen::ResetFirst || screen_ == Screen::ResetFinal)
    title = tr(STR_POKEMON_SETTINGS);
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
