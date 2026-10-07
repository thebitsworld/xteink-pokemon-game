#pragma once

#include <array>
#include <string>
#include <vector>

#include "HomeCoverCache.h"
#include "RecentBooksStore.h"
#include "UiAppHost.h"
#include "components/bars/tab-bar.h"
#include "components/media/book-card.h"
#include "components/media/cover-grid.h"
#if defined(CROSSINK_ENABLE_POKEMON)
#include "pokemon/PokemonService.h"
#endif

// The icons along the bottom of the cover grid, in order.
enum class CoverGridTab : uint8_t { Files, Library, Opds, Applications, Slideshow, Transfer, Settings };

class CoverGridHomeUi final : public UiAppHost<16, 1> {
 public:
  using UiScreen = UiAppHost<16, 1>::Screen;
  static constexpr int THUMB_HEIGHT = 400;
  static constexpr int GRID_COLUMNS = 3;
  static constexpr int GRID_ROWS = 2;
#if defined(CROSSINK_ENABLE_POKEMON)
  // The last grid cell opens the Pokemon game, showing the party leader.
  static constexpr int POKEMON_TILES = 1;
#else
  static constexpr int POKEMON_TILES = 0;
#endif
  static constexpr int MAX_BOOKS = 1 + GRID_COLUMNS * GRID_ROWS - POKEMON_TILES;
  static constexpr int MAX_TABS = 7;
  static_assert(MAX_BOOKS <= HomeCoverCache::MAX_COVERS);
  explicit CoverGridHomeUi(GfxRenderer& renderer);
  void begin(const std::vector<RecentBook>& books, bool hasOpds, bool hasContinueReading, float featuredProgress);
  void refreshCoverPaths();
  void setSelection(int selection) { selected = selection; }
#if defined(CROSSINK_ENABLE_POKEMON)
  void setPokemonSnapshot(const pokemon::PokemonDashboardSnapshot* snapshot) { pokemonSnapshot = snapshot; }
#endif
  // Selection values: the books first, then the Pokemon tile (when shown), then the tabs.
  int tileCount() const { return books != nullptr && !books->empty() ? POKEMON_TILES : 0; }
  int tabCount() const { return tabTotal; }
  CoverGridTab tabAt(int index) const { return tabOrder[index]; }
  int tabIndexOf(CoverGridTab tab) const;
  int selectedAction(const MappedInputManager& input);
  // Exact generation size for a slot, recorded during draw. Rescaling a
  // dithered 1-bit image aliases badly.
  int thumbWidthFor(size_t index) const;
  int thumbHeightFor(size_t index) const;
  bool takeThumbHeightsChanged();

 private:
  static void screenFn(UiScreen& screen, void* user);
  static void onAction(const freeink::ui::ActionEvent& event, void* user);
  void draw(UiScreen& screen);
  void drawHeaderBand();
  void drawEmpty(UiScreen& screen);
  void drawCurrent(UiScreen& screen, freeink::ui::Rect rect, int coverRowHeight);
  void drawGrid(UiScreen& screen);
  freeink::ui::Rect layoutGrid(freeink::ui::Rect rect);
  void drawTabs(UiScreen& screen, freeink::ui::Rect rect);
  void buildTabs();
#if defined(CROSSINK_ENABLE_POKEMON)
  void paintPokemonTile(freeink::ui::Rect rect);
#endif
  bool paintFramedCover(freeink::ui::DrawTarget& target, freeink::ui::Rect rect, size_t index);
  void refreshCoverPath(size_t index);
  void noteThumbSize(size_t index, int slotWidth, int slotHeight);

  HomeCoverCache coverCache;
  GfxRenderer& renderer;
  const std::vector<RecentBook>* books = nullptr;
  std::array<std::string, MAX_BOOKS> coverPaths;
  std::array<int, MAX_BOOKS> thumbWidths{};
  std::array<int, MAX_BOOKS> thumbHeights{};
  bool thumbHeightsChanged = false;
  int selected = 0;
  int pending = -1;
  int progress = -1;
  bool hasOpds = false;
  char progressText[12]{};
  // Component styles and interaction tables stay off the render task's stack.
  freeink::ui::BookCardProps card;
  freeink::ui::CoverGridProps grid;
  freeink::ui::Rect gridBounds{};
  freeink::ui::TabBarProps tabs;
  std::array<freeink::ui::TabItem, MAX_TABS> tabItems;
  std::array<CoverGridTab, MAX_TABS> tabOrder{};
  int tabTotal = 0;
#if defined(CROSSINK_ENABLE_POKEMON)
  const pokemon::PokemonDashboardSnapshot* pokemonSnapshot = nullptr;
#endif
};
