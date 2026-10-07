#include "CoverGridHomeUi.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "FeatureToggles.h"
#include "MappedInputManager.h"
#include "UITheme.h"
#include "components/icons/homeExtraIcons.h"
#include "components/icons/listIcons.h"
#if defined(CROSSINK_ENABLE_POKEMON)
#include <PokemonSpecies.h>
#include <Utf8.h>

#include <cstring>

#include "components/pokemon/PokemonArt.h"
#include "fontIds.h"
#endif

namespace fui = freeink::ui;
namespace {
constexpr fui::ActionId SELECT = 1;
constexpr int16_t COVER_CELL_INSET = 6;
}  // namespace

CoverGridHomeUi::CoverGridHomeUi(GfxRenderer& renderer)
    : UiAppHost(renderer), coverCache(renderer), renderer(renderer) {}

void CoverGridHomeUi::begin(const std::vector<RecentBook>& recent, bool opds, bool continuing, float featuredProgress) {
  books = &recent;
  hasOpds = opds;
  pokemonTile = features::pokemonGame();
  buildTabs();
  if (!recent.empty()) coverCache.begin();
  reset();
  app.on(SELECT, &CoverGridHomeUi::onAction, this);
  app.setScreen(&CoverGridHomeUi::screenFn, this);
  refreshCoverPaths();
  progress = continuing && featuredProgress >= 0 ? static_cast<int>(featuredProgress + 0.5f) : -1;
  if (progress >= 0) snprintf(progressText, sizeof(progressText), "%d%%", progress);
}

void CoverGridHomeUi::refreshCoverPaths() {
  coverCache.invalidate();
  for (size_t i = 0; i < books->size() && i < coverPaths.size(); ++i) refreshCoverPath(i);
}

void CoverGridHomeUi::refreshCoverPath(size_t index) {
  if (index >= books->size() || index >= coverPaths.size()) return;
  coverCache.invalidate(index);
  coverPaths[index] =
      thumbWidths[index] > 0 && thumbHeights[index] > 0
          ? UITheme::getCoverThumbPath((*books)[index].coverBmpPath, thumbWidths[index], thumbHeights[index], false)
          : std::string();
}

int CoverGridHomeUi::bookLimit() { return MAX_BOOKS - (features::pokemonGame() ? 1 : 0); }

int CoverGridHomeUi::thumbWidthFor(size_t index) const {
  return index < thumbWidths.size() && thumbWidths[index] > 0 ? thumbWidths[index] : THUMB_HEIGHT * 2 / 3;
}

int CoverGridHomeUi::thumbHeightFor(size_t index) const {
  return index < thumbHeights.size() && thumbHeights[index] > 0 ? thumbHeights[index] : THUMB_HEIGHT;
}

bool CoverGridHomeUi::takeThumbHeightsChanged() { return std::exchange(thumbHeightsChanged, false); }

void CoverGridHomeUi::noteThumbSize(size_t index, int slotWidth, int slotHeight) {
  if (index >= thumbHeights.size()) return;
  const int width = std::max(1, slotWidth);
  const int height = std::max(1, slotHeight);
  if (thumbWidths[index] != width || thumbHeights[index] != height) {
    thumbWidths[index] = width;
    thumbHeights[index] = height;
    thumbHeightsChanged = true;
    refreshCoverPath(index);
  }
}

void CoverGridHomeUi::onAction(const fui::ActionEvent& event, void* user) {
  auto& self = *static_cast<CoverGridHomeUi*>(user);
  self.pending = event.value;
  self.app.clearTapFlash();
}

int CoverGridHomeUi::selectedAction(const MappedInputManager& input) {
  pending = -1;
  fui::ActionEvent event{};
  return routeTouch(input, event) ? pending : -1;
}

void CoverGridHomeUi::screenFn(UiScreen& screen, void* user) { static_cast<CoverGridHomeUi*>(user)->draw(screen); }

void CoverGridHomeUi::draw(UiScreen& screen) {
  coverCache.prepare();
  const auto& theme = screen.theme();
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, !BoardConfig::hasTouch());
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y), static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width),
      static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height), static_cast<int16_t>(safe.x)});
  const int16_t hInset = std::max<int16_t>(
      0, static_cast<int16_t>(UITheme::getInstance().getMetrics().headerSidePadding - COVER_CELL_INSET - safe.x));
  const int16_t topInset =
      BoardConfig::hasTouch() ? static_cast<int16_t>(theme.spaceLg) : static_cast<int16_t>(theme.spaceSm + 4);
  screen.insetContent(fui::Insets{topInset, hInset, theme.spaceSm, hInset});
  const bool landscape = renderer.getScreenWidth() > renderer.getScreenHeight();
  screen.takeTop(UITheme::getInstance().getMetrics().batteryBarHeight, theme.spaceSm);
  const int16_t tabGap = BoardConfig::hasTouch() ? theme.spaceSm : static_cast<int16_t>(4);
  auto tabRect = screen.takeBottom(72, tabGap);
  if (books->empty()) {
    drawTabs(screen, tabRect.inset(fui::Insets{0, COVER_CELL_INSET, 0, COVER_CELL_INSET}));
    if (tileCount() > 0) drawEmptyTile(screen, screen.body());
    drawEmpty(screen);
    drawHeaderBand();
    return;
  }
  const fui::Rect body = screen.body();
  const int rowGap = std::max<int>(4, body.width / 100);
  grid.gap = grid.rowGap = rowGap;
  const int coverRowHeight = std::max(1, (body.height - theme.spaceSm - rowGap) / 3);
  const int16_t featuredHeight = std::min<int>(
      body.height, std::max<int>(coverRowHeight, screen.target().lineHeight(theme.bodyText.font) * (landscape ? 1 : 2) +
                                                     screen.target().lineHeight(theme.smallText.font) * 2 + 32));
  const auto featuredRect = screen.takeTop(featuredHeight, theme.spaceSm);
  const int gridRowHeight = std::max(1, (screen.body().height - rowGap) / GRID_ROWS);
  drawCurrent(screen, featuredRect, std::min(coverRowHeight, gridRowHeight));
  drawGrid(screen);
  tabRect.x = gridBounds.x + grid.cellInset.left;
  tabRect.width = gridBounds.width - grid.cellInset.left - grid.cellInset.right;
  drawTabs(screen, tabRect);
  drawHeaderBand();
}

void CoverGridHomeUi::drawHeaderBand() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.homeTopPadding}, nullptr);
}

void CoverGridHomeUi::drawEmpty(UiScreen& screen) {
  const auto& theme = screen.theme();
  const auto body = screen.body();
  auto title = theme.titleText;
  title.bold = true;
  title.align = fui::TextAlign::Center;
  auto message = theme.bodyText;
  message.align = fui::TextAlign::Center;
  constexpr int16_t ICON_SIZE = 32;
  const int16_t titleHeight = screen.target().lineHeight(title.font);
  const int16_t messageHeight = screen.target().lineHeight(message.font);
  const int16_t contentHeight = ICON_SIZE + theme.spaceLg + titleHeight + theme.spaceSm + messageHeight;
  int16_t y = body.y + std::max(0, (body.height - contentHeight) / 2);
  drawLucideIcon(renderer, icon_book_32, body.x + (body.width - ICON_SIZE) / 2, y);
  y += ICON_SIZE + theme.spaceLg;
  screen.target().text(fui::Rect{body.x, y, body.width, titleHeight}, tr(STR_NO_OPEN_BOOK), title);
  y += titleHeight + theme.spaceSm;
  screen.target().text(fui::Rect{body.x, y, body.width, messageHeight}, tr(STR_START_READING), message);
}

// With no books yet, the Pokemon tile sits alone at the bottom of the body,
// sized like a grid cell, and the "no open book" message centres above it.
void CoverGridHomeUi::drawEmptyTile(UiScreen& screen, const fui::Rect body) {
  const int cellWidth = body.width / GRID_COLUMNS;
  card.coverSize.width = std::max(1, cellWidth - 2 * COVER_CELL_INSET);
  card.coverSize.height = std::max(1, std::min(card.coverSize.width * 3 / 2, body.height / 2 - 12));
  card.styles = screen.theme().listRow;
  grid.cellInset = fui::Insets{COVER_CELL_INSET, COVER_CELL_INSET, COVER_CELL_INSET, COVER_CELL_INSET};
  grid.coverSize = card.coverSize;
  grid.rowHeight = grid.coverSize.height + 12;
  const int16_t height = static_cast<int16_t>(grid.rowHeight);
  gridBounds = fui::Rect{static_cast<int16_t>(body.x + (body.width - cellWidth) / 2),
                         static_cast<int16_t>(body.bottom() - height), static_cast<int16_t>(cellWidth), height};
  screen.takeBottom(height, screen.theme().spaceLg);
  drawGrid(screen);
}

void CoverGridHomeUi::drawCurrent(UiScreen& screen, fui::Rect rect, const int coverRowHeight) {
  const auto& theme = screen.theme();
  const auto& book = books->front();
  card.title = book.title.c_str();
  card.author = book.author.empty() ? nullptr : book.author.c_str();
  card.meta = nullptr;
  card.progressLabel = progress >= 0 ? progressText : nullptr;
  card.centerTextOnCover = true;
  card.progress = std::max(0, progress);
  card.progressMax = progress >= 0 ? 100 : 0;
  card.action = SELECT;
  card.state = fui::StateNormal;
  card.styles = theme.listRow;
  card.styles.selected.background = fui::Paint::dither(fui::Color::LightGray);
  card.styles.selected.border = fui::Paint::dither(fui::Color::LightGray);
  card.styles.selected.foreground = fui::Paint::solid(fui::Color::Black);
  card.styles.selected.radius = theme.listRowRadius;
  card.styles.active = card.styles.selected;
  card.titleText = theme.bodyText;
  card.titleText.maxLines = renderer.getScreenWidth() > renderer.getScreenHeight() ? 1 : 2;
  card.authorText = theme.smallText;
  card.progressText = theme.smallText;
  card.progressHeight = 6;
  card.padding = fui::Insets{6, 6, 6, 6};
  card.gap = theme.spaceLg + theme.spaceSm;
  const int maxCoverWidth = rect.width / GRID_COLUMNS - 2 * COVER_CELL_INSET;
  card.coverSize.height = std::max(1, std::min(std::min<int>(rect.height, coverRowHeight) - 12, maxCoverWidth * 3 / 2));
  card.coverSize.width = std::max(1, card.coverSize.height * 2 / 3);
  noteThumbSize(0, card.coverSize.width, card.coverSize.height);
  gridBounds = layoutGrid(screen.body());
  rect.x = gridBounds.x;
  rect.width = gridBounds.width;
  card.coverPainterUserData = this;
  card.coverPainter = [](fui::DrawTarget& target, fui::Rect cover, const fui::BookCardProps&, void* user) {
    return static_cast<CoverGridHomeUi*>(user)->paintFramedCover(target, cover, 0);
  };
  fui::bookCard(screen.frame(), rect, card);

  if (selected == 0 && !BoardConfig::hasTouch()) {
    const int16_t barHeight = card.coverSize.height;
    screen.target().fill(fui::Rect{static_cast<int16_t>(rect.x - 9),
                                   static_cast<int16_t>(rect.y + (rect.height - barHeight) / 2), 3, barHeight},
                         fui::Paint::dither(fui::Color::LightGray));
  }
}

fui::Rect CoverGridHomeUi::layoutGrid(fui::Rect rect) {
  grid.cellInset = fui::Insets{COVER_CELL_INSET, COVER_CELL_INSET, COVER_CELL_INSET, COVER_CELL_INSET};
  grid.coverSize = card.coverSize;
  grid.rowHeight = grid.coverSize.height + 12;
  rect.height = GRID_ROWS * grid.rowHeight + (GRID_ROWS - 1) * grid.rowGap;
  return rect;
}

void CoverGridHomeUi::drawGrid(UiScreen& screen) {
  const auto rect = gridBounds;
  grid.count = (books->size() > 1 ? books->size() - 1 : 0) + tileCount();
  grid.itemProviderUserData = this;
  grid.columns = books->empty() ? 1 : GRID_COLUMNS;
  grid.columnLayout = fui::CoverGridColumnLayout::SpaceBetween;
  grid.action = SELECT;
  grid.inputMask = fui::InputTouch;
  grid.selectedIndex = selected >= firstGridValue() && selected < static_cast<int>(books->size()) + tileCount()
                           ? selected - firstGridValue()
                           : -1;
  grid.selectionIndicator = fui::CoverGridSelectionIndicator::CoverFrame;
  grid.selectedCoverFrameGap = 6;
  grid.selectedCoverFrameWidth = 8;
  grid.cellStyles = card.styles;
  grid.labelHeight = 0;
  grid.labelGap = 0;
  for (size_t i = 1; i < thumbHeights.size(); ++i) noteThumbSize(i, grid.coverSize.width, grid.coverSize.height);
  grid.scrollIndicator = false;
  // Grid cell values are selection values: the books after the featured one, then the tile.
  grid.itemProvider = [](uint16_t index, void* user) {
    return fui::coverGridItem(nullptr, index + static_cast<CoverGridHomeUi*>(user)->firstGridValue());
  };
  grid.coverPainterUserData = this;
  grid.coverPainter = [](fui::DrawTarget& target, fui::Rect cover, const fui::CoverGridItem&, uint16_t index,
                         void* user) {
    auto& self = *static_cast<CoverGridHomeUi*>(user);
#if defined(CROSSINK_ENABLE_POKEMON)
    if (index + self.firstGridValue() >= static_cast<int>(self.books->size())) {
      self.paintPokemonTile(cover);
      return true;
    }
#endif
    return self.paintFramedCover(target, cover, index + 1);
  };
  fui::coverGrid(screen.frame(), rect, grid);
}

void CoverGridHomeUi::buildTabs() {
  tabTotal = 0;
  const auto add = [this](const CoverGridTab tab) { tabOrder[tabTotal++] = tab; };
  add(CoverGridTab::Files);
  add(CoverGridTab::Library);
  if (hasOpds) add(CoverGridTab::Opds);
#if defined(CROSSINK_ENABLE_POKEMON) && defined(CROSSINK_ENABLE_LUA_APPS)
  if (features::applications()) add(CoverGridTab::Applications);
#endif
  if (features::slideshow()) add(CoverGridTab::Slideshow);
  add(CoverGridTab::Transfer);
  add(CoverGridTab::Settings);
}

int CoverGridHomeUi::tabIndexOf(const CoverGridTab tab) const {
  for (int i = 0; i < tabTotal; ++i) {
    if (tabOrder[i] == tab) return i;
  }
  return -1;
}

void CoverGridHomeUi::drawTabs(UiScreen& screen, fui::Rect rect) {
  const int first = static_cast<int>(books->size()) + tileCount();
  for (int i = 0; i < tabTotal; ++i) {
    auto& tab = tabItems[i];
    tab.value = first + i;
    tab.selected = selected == tab.value;
    tab.label = nullptr;
  }
  tabs.tabs = tabItems.data();
  tabs.count = tabTotal;
  tabs.layout = fui::TabBarLayout::SpaceBetween;
  tabs.action = SELECT;
  tabs.inputMask = fui::InputTouch;
  tabs.iconSize = 32;
  tabs.iconPainterUserData = this;
  tabs.iconPainter = [](fui::DrawTarget&, fui::Rect iconRect, const fui::TabItem& tab, uint8_t, void* user) {
    static constexpr const freeink::Icon* ICONS[] = {&icon_folder_32,  &icon_landmark_32,      &icon_lyra_library_32,
                                                     &icon_gamepad_32, &icon_image_32,         &icon_lyra_transfer_32,
                                                     &icon_lyra_settings_32};
    const auto& self = *static_cast<CoverGridHomeUi*>(user);
    const int index = tab.value - static_cast<int>(self.books->size()) - self.tileCount();
    drawLucideIcon(self.renderer, *ICONS[static_cast<int>(self.tabOrder[index])], iconRect.x, iconRect.y);
    return true;
  };
  tabs.tabStyles.normal.background = fui::Paint::solid(fui::Color::White);
  tabs.tabStyles.selected.background = fui::Paint::solid(fui::Color::White);
  tabs.selectedUnderline = 2;
  tabs.distributedSlotWidth = 0;
  fui::tabBar(screen.frame(), rect, tabs);
}

bool CoverGridHomeUi::paintFramedCover(fui::DrawTarget& target, fui::Rect rect, size_t index) {
  constexpr int16_t SHADOW_OFFSET = 2;
  const auto ink = fui::Paint::solid(fui::Color::Black);
  target.fill(fui::Rect{rect.right(), static_cast<int16_t>(rect.y + SHADOW_OFFSET), SHADOW_OFFSET, rect.height}, ink);
  target.fill(fui::Rect{static_cast<int16_t>(rect.x + SHADOW_OFFSET), rect.bottom(), rect.width, SHADOW_OFFSET}, ink);
  const bool drawn = index < coverPaths.size() && coverCache.paint(rect, index, coverPaths[index]);
  target.stroke(rect, ink, 1, 0);
  return drawn;
}

#if defined(CROSSINK_ENABLE_POKEMON)
namespace {
// Copies `source` into `out`, cut short (on a UTF-8 boundary) to fit `width`.
template <size_t Size>
const char* fitText(const GfxRenderer& renderer, const int font, const char* source, const int width,
                    char (&out)[Size], const EpdFontFamily::Style style) {
  snprintf(out, Size, "%s", source == nullptr ? "" : source);
  int length = utf8SafeTruncateBuffer(out, static_cast<int>(strlen(out)));
  out[length] = '\0';
  while (length > 0 && renderer.getTextWidth(font, out, style) > width) {
    length = utf8SafeTruncateBuffer(out, length - 1);
    out[length] = '\0';
  }
  return out;
}
}  // namespace

// The Pokemon tile: the party leader with its name and level, or a Poke Ball
// and an invitation before a starter is chosen. A "!" marks something waiting
// in the game.
void CoverGridHomeUi::paintPokemonTile(const fui::Rect rect) {
  constexpr int16_t SHADOW_OFFSET = 2;
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  renderer.fillRect(rect.right(), rect.y + SHADOW_OFFSET, SHADOW_OFFSET, rect.height, true);
  renderer.fillRect(rect.x + SHADOW_OFFSET, rect.bottom(), rect.width, SHADOW_OFFSET, true);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height, true);

  const auto* snapshot = pokemonSnapshot;
  const bool hasLeader = snapshot != nullptr && snapshot->leader.recordId != 0;
  const int lineBold = renderer.getLineHeight(UI_12_FONT_ID);
  const int lineSmall = renderer.getLineHeight(UI_10_FONT_ID);
  const int textWidth = rect.width - 8;

  // The words first: a name and level, or "Pokemon" and the invitation to
  // pick a starter (on two lines when it does not fit on one).
  char title[40];
  char detail[48];
  char detail2[48] = "";
  int titleFont = UI_12_FONT_ID;
  if (hasLeader) {
    const pokemon::SpeciesData* species = pokemon::speciesData(snapshot->leader.speciesId);
    const char* name = snapshot->leader.nickname[0] != '\0' ? snapshot->leader.nickname.data()
                                                            : (species == nullptr ? "???" : species->name);
    // A long name drops to the smaller font before it is cut short.
    if (renderer.getTextWidth(titleFont, name, EpdFontFamily::BOLD) > textWidth) titleFont = UI_10_FONT_ID;
    fitText(renderer, titleFont, name, textWidth, title, EpdFontFamily::BOLD);
    char level[24];
    snprintf(level, sizeof(level), "%s %u", tr(STR_POKEMON_LEVEL),
             static_cast<unsigned>(pokemon::levelXpProgress(snapshot->leader.totalXp).level));
    fitText(renderer, UI_10_FONT_ID, level, textWidth, detail, EpdFontFamily::REGULAR);
  } else {
    fitText(renderer, UI_12_FONT_ID, tr(STR_POKEMON), textWidth, title, EpdFontFamily::BOLD);
    const char* hint = tr(STR_POKEMON_CHOOSE_STARTER);
    snprintf(detail, sizeof(detail), "%s", hint);
    if (renderer.getTextWidth(UI_10_FONT_ID, detail) > textWidth) {
      // Break at the last space that leaves a first line that fits.
      for (char* space = strrchr(detail, ' '); space != nullptr; space = strrchr(detail, ' ')) {
        *space = '\0';
        if (renderer.getTextWidth(UI_10_FONT_ID, detail) <= textWidth) {
          fitText(renderer, UI_10_FONT_ID, hint + (space - detail) + 1, textWidth, detail2, EpdFontFamily::REGULAR);
          break;
        }
      }
      if (detail2[0] == '\0') fitText(renderer, UI_10_FONT_ID, hint, textWidth, detail, EpdFontFamily::REGULAR);
    }
  }
  const int textHeight = lineBold + lineSmall + (detail2[0] != '\0' ? lineSmall : 0);
  const int side = std::max(1, std::min<int>(rect.width - 8, rect.height - textHeight - 16));
  const int artX = rect.x + (rect.width - side) / 2;
  const int artY = rect.y + std::max(4, (rect.height - side - textHeight - 4) / 2);

  if (hasLeader) {
    pokemon::drawPokemonSpeciesArt(renderer, snapshot->leader.speciesId, true, Rect{artX, artY, side, side});
  } else {
    const freeink::Icon& ball = side >= 64 ? icon_pokeball_64 : icon_pokeball_32;
    drawLucideIcon(renderer, ball, artX + (side - ball.w) / 2, artY + (side - ball.h) / 2);
  }
  int y = artY + side + 4;
  renderer.drawText(titleFont, rect.x + (rect.width - renderer.getTextWidth(titleFont, title, EpdFontFamily::BOLD)) / 2,
                    y, title, true, EpdFontFamily::BOLD);
  y += lineBold;
  for (const char* line : {static_cast<const char*>(detail), static_cast<const char*>(detail2)}) {
    if (line[0] == '\0') continue;
    renderer.drawText(UI_10_FONT_ID, rect.x + (rect.width - renderer.getTextWidth(UI_10_FONT_ID, line)) / 2, y, line);
    y += lineSmall;
  }
  if (hasLeader && snapshot->notice != pokemon::DashboardNotice::None) {
    renderer.drawText(UI_12_FONT_ID, rect.right() - 14, rect.y + 4, "!", true, EpdFontFamily::BOLD);
  }
}
#endif
