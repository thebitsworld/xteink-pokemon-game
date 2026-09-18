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
    PcReleaseConfirm,
    Bag,
    BagEvolution,
    BagMedicine,
    BagBalls,
    BagMachine,
    ItemTarget,
    PpUpSlot,
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
    Settings,
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
    // Entered from Screen::BattleBag (Stage 18) rather than the out-of-battle
    // Screen::Bag - same item-then-target flow through Screen::ItemTarget,
    // but the outcome returns to Screen::Battle (and syncs battlePlayer_ if
    // the target was the active combatant) instead of Screen::Party.
    BattleMedicine,
    // PP Up - lives as a trailing synthetic row inside Screen::BagMedicine's
    // list (its count lives in its own ppUpCount field, not bagCounts, so it
    // can't join the generic per-category item walk). Unlike every other
    // category there's only ever one PP Up "item" to pick from, so selecting
    // it goes straight to Screen::ItemTarget, then straight to
    // Screen::PpUpSlot (always - no MovesetFull-style retry branching, every
    // occupied slot is valid).
    PpUp,
  };

  static constexpr uint8_t ROW_CAPACITY = 10;
  using UiApp = freeink::ui::FreeInkApp<24, 8>;

  void loadInitialScreen();
  void setScreen(Screen screen, int selected = 0);
  bool refreshSnapshot();
  void activate();
  void goBack();
  // `routeSuccessThroughPendingEvent`: true (the default) means a successful
  // rename/starter-naming lands on Screen::Menu, or Screen::Event if a
  // reading-time event happens to be queued right now - the behavior the
  // post-catch/starter naming flow wants. false means it lands on
  // `cancelScreen` instead, exactly like every sibling CollectionAction
  // (Deposit/Withdraw/EvolutionPrompts) already returns the player to where
  // they came from - what the Rename-an-already-owned-Pokemon flow
  // (Screen::Actions) wants instead. Both the success AND the SD-write-
  // failure branch use the same rule - see docs/development/pokemon-gen1-
  // audit-round6.md item 2.6.
  void openNickname(uint32_t recordId, bool starter, Screen cancelScreen,
                    bool routeSuccessThroughPendingEvent = true);
  void finishStarter(const char* nickname);
  void showMessage(const char* message, Screen returnScreen);
  void buildUi(freeink::ui::FreeInkApp<24, 8>::ScreenType& screen);
  void buildRows();
  void buildList(freeink::ui::FreeInkApp<24, 8>::ScreenType& screen);
  void renderFocused();
  void renderRowArt();
  void renderHeaderAndHints();
  // `preserveSideEffects`: true when this (re)builds a BattleCombatant for a
  // Pokemon continuing the SAME already-in-progress battle (a voluntary/
  // forced mid-battle switch, or the opponent's team advancing to its next
  // member) - Reflect/Light Screen/Mist protect the whole SIDE in real Gen 1
  // and must survive that kind of switch. Left false (the default) at every
  // fresh-battle-start call site (enterBattle()/enterGymBattle()), where
  // whatever is sitting in battlePlayer_/battleOpponent_ is stale leftover
  // state from a previous, already-concluded battle and must NOT carry
  // forward - see docs/development/pokemon-gen1-audit-round6.md item 2.5.
  bool setupBattlePlayer(int slot, bool preserveSideEffects = false);
  void setupBattleOpponent(uint16_t speciesId, uint8_t level, std::span<const uint8_t> fixedMoves = {},
                           pokemon::Gender gender = pokemon::Gender::Unknown, bool isShiny = false,
                           bool preserveSideEffects = false);
  bool enterBattle(const pokemon::PendingEvent& pending);
  bool enterGymBattle(uint8_t gymIndex);
  void savePlayerBattleEntry();
  void resolveBattleAsPass();
  void finishItemUseMidBattle(const char* usedLine);
  void finishBattleAfterWildFainted();
  void finishBattleAfterPlayerFainted();
  // playerAlsoFainted: the exchange that produced this win also left the
  // player's own active Pokemon at 0 HP (a mutual KO the player caused - see
  // docs/development/pokemon-gen1-audit-round4.md bug 2.9). Only ever true
  // when called from finishBattleAfterWildFainted()'s gym path - the
  // Whirlwind/Roar-forced-switch call site can't produce a fainted attacker.
  void advanceGymOpponentOrFinish(bool playerAlsoFainted = false);
  void finishGymChallenge(bool won, bool playerAlsoFainted = false);
  void buildBattleLog(const pokemon::BattleTurnResult& result);
  void renderBattleHud();
  void renderBattleMenu();
  void renderBattleMoveMenu();
  int battleMenuTop() const;
  Rect battleGridCellRect(int index) const;
  void renderMenuGrid();
  // Compact "how close to a guaranteed drop" readout, drawn below the Menu
  // grid in whatever screen space it leaves unused. Purely a display of
  // PokemonState fields the engine already persists (encounterMisses/
  // ballMisses/medicineMisses/machineMisses/itemMisses) - no new save data.
  void renderMenuPityBars();
  void renderBagGrid();
  void renderPcOrderButtons();
  int buttonGridTop() const;
  Rect buttonGridCellRect(int index, int columns = 2) const;
  void drawGridButton(const Rect& cell, bool selected, const char* label);
  int battlePlayerMoveCount() const;
  bool battlePlayerHasAnyUsablePp() const;
  void resolveBattlePlayerMoveTurn(uint8_t moveSlot);
  // Trainer AI (gym/Elite Four only, not the Champion - see
  // gymTeamOrder_'s doc comment - and never a wild encounter): decides
  // whether the opponent should use a healing item or proactively switch
  // instead of attacking this turn. If so, mutates battleOpponent_ (and
  // gymTeamOrder_/opponentHealChargesRemaining_ as needed) and writes a
  // "Trainer used ...!"-style line into `buffer`, returning true; otherwise
  // leaves everything untouched and returns false.
  bool trainerAiShouldActInsteadOfMoveThisTurn(char* buffer, size_t size);
  // Stage 13: which party members can currently fight (BattleRecordEntry's
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
  bool showsMachineCapabilityRows() const;
  int rowsPerPage() const;
  int pageStart() const;
  void renderPartyRowHealth(int rowY, const pokemon::PokemonRecord& record, bool drawNameLine);
  void renderPartyRowMachineCapability(int rowY, const pokemon::PokemonRecord& record);
  bool isListScreen() const;
  uint32_t selectedRecordId() const;
  pokemon::PokemonRecord selectedRecord() const;
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
  // Cached alongside focusedRecordId_ at every one of its 3 assignment
  // sites (all already have the full record in RAM already - from
  // snapshot_.party or pcPage_, not a fresh read) so the Moveset/
  // MovesetPick/TmReplaceSlot/PpUpSlot screens don't call readRecord() once
  // per row/frame/keypress for data that never changes while focused on the
  // same Pokemon.
  pokemon::PokemonRecord focusedRecord_{};
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
  // gymTeamOrder_[i] is which real gymTeamFor(gymChallengeIndex_) index sits
  // at position i - identity-initialized (gymTeamOrder_[i] == i) whenever a
  // gym challenge starts, in enterGymBattle(). The trainer AI's own
  // voluntary switch (see trainerAiShouldActInsteadOfMoveThisTurn()) swaps
  // entries in the still-to-fight suffix (indices > gymChallengeTeamProgress_)
  // rather than ever changing what gymChallengeTeamProgress_ itself means -
  // it's still simply "how many of the trainer's team have been defeated,"
  // now reached through this indirection instead of indexing the team
  // directly. Never touched for a wild encounter (gymChallengeIndex_ == 0).
  std::array<uint8_t, pokemon::MAX_GYM_TEAM_SIZE> gymTeamOrder_{};
  // How many more times this trainer battle's AI can use a healing item
  // (see trainerAiShouldActInsteadOfMoveThisTurn()) - reset once per gym
  // challenge in enterGymBattle(), never replenished mid-fight. A small,
  // deliberately simplified stand-in for the real games' actual per-trainer
  // item stock, which this project has no data for at all.
  uint8_t opponentHealChargesRemaining_ = 0;
  int battlePartySlot_ = 0;               // which snapshot_.party[] slot is currently battlePlayer_
  bool forcedBattleSwitch_ = false;       // true while the active Pokemon just fainted - Back can't cancel out
  // How many times RUN has already failed THIS wild battle - Gen 1's real
  // escape odds improve with each failed attempt (see
  // PokemonService::attemptRunFromBattle()'s doc comment). Reset to 0 at the
  // start of every new battle (enterBattle()/enterGymBattle(), though a gym
  // RUN never consults this at all - see Screen::Battle's RUN handling).
  uint8_t battleRunAttempts_ = 0;
  std::array<freeink::ui::ListItem, ROW_CAPACITY> rows_{};
  std::array<std::array<char, 56>, ROW_CAPACITY> labels_{};
  std::array<std::array<char, 32>, ROW_CAPACITY> values_{};
  // Backing storage for ListItem::subtitle on rows that need one (Pokedex's
  // dex number, BagMachine's taught-move name) - mirrors labels_/values_.
  // GymList's own subtitle ("Elite Four"/"Champion") reuses values_ instead
  // since its value slot is a static tr() string with no buffer of its own
  // to free up; these screens' value slots aren't always free that way, so
  // they get this dedicated buffer.
  std::array<std::array<char, 24>, ROW_CAPACITY> subtitles_{};
  Rect listBounds_{};
  int rowHeight_ = 0;
  bool cleanRefreshNeeded_ = true;
  freeink::ui::GfxRendererTarget uiTarget_;
  UiApp app_;
  ButtonNavigator navigator_;
  std::atomic<bool> uiReady_{false};
};

#endif
