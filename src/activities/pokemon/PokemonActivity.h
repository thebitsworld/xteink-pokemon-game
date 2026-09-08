#pragma once

#if defined(CROSSINK_ENABLE_POKEMON)

#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>
#include <PokemonPromptContext.h>

#include <array>
#include <atomic>
#include <cstdint>

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
    Pc,
    PcOrder,
    Bag,
    ItemTarget,
    Pokedex,
    PokedexDetail,
    Event,
    Battle,
    BattleMoves,
    BattleBalls,
    GymList,
    Badges,
    ResetFirst,
    ResetFinal,
    Message,
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
  bool setupBattlePlayer();
  void setupBattleOpponent(uint16_t speciesId, uint8_t level);
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
  int logicalCount() const;
  int listTop() const;
  int rowsPerPage() const;
  int pageStart() const;
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
  bool bagSelectionIsMachine_ = false;
  uint8_t selectedMachineItemId_ = 0;
  uint8_t selectedMachineMoveId_ = 0;
  char message_[96]{};
  pokemon::BattleCombatant battlePlayer_{};
  pokemon::BattleCombatant battleOpponent_{};
  char battleLog_[160]{};
  uint8_t gymChallengeIndex_ = 0;       // 0 = not fighting a gym/Elite Four right now
  uint8_t gymChallengeTeamProgress_ = 0;  // index of the opponent team member currently out
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
