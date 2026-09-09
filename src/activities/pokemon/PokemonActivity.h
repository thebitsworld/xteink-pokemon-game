#pragma once

#if defined(CROSSINK_ENABLE_POKEMON)

#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>
#include <PokemonPromptContext.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <span>

#include "activities/Activity.h"
#include "components/themes/BaseTheme.h"
#include "pokemon/PokemonService.h"
#include "util/ButtonNavigator.h"

class PokemonActivity final : public Activity {
 public:
  PokemonActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Screen : uint8_t {
    Starter,
    Gender,
    NicknameQuestion,
    Menu,
    Party,
    Summary,
    Actions,
    Move,
    Moveset,
    MovesetPick,
    TmReplaceSlot,
    Pc,
    PcOrder,
    Bag,
    BagEvolution,
    BagMedicine,
    BagMachine,
    ItemTarget,
    Pokedex,
    PokedexDetail,
    Event,
    Battle,
    BattleMoves,
    BattleBalls,
    BattleBag,
    BattleSwitch,
    GymList,
    Badges,
    ResetFirst,
    ResetFinal,
    Message,
  };

  // Which Bag category ItemTarget was entered from - determines both what
  // activate() does with the selection and which screen goBack() returns
  // to (Bag itself is just the 3-way category picker, not a target list).
  enum class BagCategory : uint8_t {
    Evolution,
    Medicine,
    Machine,
    // Entered from Screen::BattleBag (GĐ18) rather than the out-of-battle
    // Screen::Bag - same item-then-target flow through Screen::ItemTarget,
    // but the outcome returns to Screen::Battle (and syncs battlePlayer_ if
    // the target was the active combatant) instead of Screen::Party.
    BattleMedicine,
  };

  static constexpr uint8_t ROW_CAPACITY = 10;
  using UiApp = freeink::ui::FreeInkApp<24, 8>;

  void loadInitialScreen();
  void setScreen(Screen screen, int selected = 0);
  bool refreshSnapshot();
  void activate();
  void goBack();
  void openNickname(uint32_t recordId, bool starter, Screen cancelScreen);
  void finishStarter(const char* nickname);
  void showMessage(const char* message, Screen returnScreen);
  void buildUi(freeink::ui::FreeInkApp<24, 8>::ScreenType& screen);
  void buildRows();
  void buildList(freeink::ui::FreeInkApp<24, 8>::ScreenType& screen);
  void renderFocused();
  void renderRowArt();
  void renderHeaderAndHints();
  bool setupBattlePlayer(int slot);
  void setupBattleOpponent(uint16_t speciesId, uint8_t level, std::span<const uint8_t> fixedMoves = {});
  bool enterBattle(const pokemon::PendingEvent& pending);
  bool enterGymBattle(uint8_t gymIndex);
  void savePlayerBattleEntry();
  void resolveBattleAsPass();
  void finishBattleAfterWildFainted();
  void finishBattleAfterPlayerFainted();
  void advanceGymOpponentOrFinish();
  void finishGymChallenge(bool won);
  void buildBattleLog(const pokemon::BattleTurnResult& result);
  void renderBattleHud();
  int battlePlayerMoveCount() const;
  // GĐ 13: which party members can currently fight (BattleRecordEntry's
  // currentHp > 0, via the read-only peekBattleMoves - no battle-store
  // writes just from checking). firstUsablePartySlot() picks who starts a
  // fresh battle; the usablePartySlot* pair excludes battlePartySlot_
  // itself, for the mid-battle switch picker.
  int firstUsablePartySlot() const;
  size_t usablePartySlotCount() const;
  int usablePartySlotAt(size_t index) const;
  int logicalCount() const;
  int listTop() const;
  int rowHeightForScreen() const;
  bool showsPartyHealthRows() const;
  int rowsPerPage() const;
  int pageStart() const;
  void renderPartyRowHealth(int rowY, const pokemon::PokemonRecord& record);
  bool isListScreen() const;
  uint32_t selectedRecordId() const;
  static void screenBuilder(UiApp::ScreenType& screen, void* user);
  static void onRow(const freeink::ui::ActionEvent& event, void* user);

  pokemon::PokemonService& service_;
  pokemon::PokemonSnapshot snapshot_{};
  std::array<pokemon::PokemonRecord, ROW_CAPACITY> pcPage_{};
  size_t pcCount_ = 0;
  pokemon::PcOrder pcOrder_ = pokemon::PcOrder::CatchDate;
  Screen screen_ = Screen::Menu;
  Screen returnScreen_ = Screen::Menu;
  Screen actionSource_ = Screen::Party;
  int selected_ = 0;
  int rowCount_ = 0;
  uint16_t starterSpecies_ = 1;
  uint16_t pokedexSpecies_ = 1;
  pokemon::Gender starterGender_ = pokemon::Gender::Male;
  uint32_t focusedRecordId_ = 0;
  pokemon::PokemonPromptContext nicknamePrompt_{};
  pokemon::EvolutionItem selectedItem_ = pokemon::EvolutionItem::None;
  BagCategory bagCategory_ = BagCategory::Evolution;
  uint8_t selectedMachineItemId_ = 0;
  uint8_t selectedMachineMoveId_ = 0;
  uint8_t selectedMedicineItemId_ = 0;
  uint8_t movesetSlot_ = 0;
  char message_[96]{};
  pokemon::BattleCombatant battlePlayer_{};
  pokemon::BattleCombatant battleOpponent_{};
  char battleLog_[160]{};
  uint8_t gymChallengeIndex_ = 0;         // 0 = not fighting a gym/Elite Four right now
  uint8_t gymChallengeTeamProgress_ = 0;  // index of the opponent team member currently out
  int battlePartySlot_ = 0;               // which snapshot_.party[] slot is currently battlePlayer_
  bool forcedBattleSwitch_ = false;       // true while the active Pokemon just fainted - Back can't cancel out
  std::array<freeink::ui::ListItem, ROW_CAPACITY> rows_{};
  std::array<std::array<char, 56>, ROW_CAPACITY> labels_{};
  std::array<std::array<char, 32>, ROW_CAPACITY> values_{};
  Rect listBounds_{};
  int rowHeight_ = 0;
  bool cleanRefreshNeeded_ = true;
  freeink::ui::GfxRendererTarget uiTarget_;
  UiApp app_;
  ButtonNavigator navigator_;
  std::atomic<bool> uiReady_{false};
};

#endif
