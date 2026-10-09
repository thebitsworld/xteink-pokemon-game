#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "SmudgeSettings.h"
#include "activities/Activity.h"
#include "activities/apps/AppSettingsActivity.h"
#include "activities/apps/AppStoreActivity.h"
#include "activities/apps/lua/AppPackage.h"
#include "activities/apps/lua/LuaAppActivity.h"
#include "components/UiAppHelpers.h"
#include "components/icons/listIcons.h"
#include "fontIds.h"

class ApplicationsActivity : public Activity {
 public:
  ApplicationsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Applications", renderer, mappedInput) {}

  void onEnter() override {
    Activity::onEnter();
    refreshMenuList();
    requestUpdate();
  }

  void onExit() override {
    visibleApps.clear();
    visibleApps.shrink_to_fit();
    Activity::onExit();
  }

  void loop() override {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
      return;
    }

    int appCount = static_cast<int>(visibleApps.size());
    if (appCount == 0) return;

    auto launchSelectedApp = [this]() {
      const auto& app = visibleApps[selectedIndex];
      const bool isSetting = app.builtIn;
      if (!isSetting) {
        SmudgeSettings::getInstance().recordAppLaunch(app.name);
      }
      startActivityForResult(makeActivity(app), [this, isSetting](const ActivityResult&) {
        if (!isSetting) {
          renderer.clearScreen();
          renderer.displayBuffer(HalDisplay::RefreshMode::FULL_REFRESH);
        }
        refreshMenuList();
        requestUpdate();
      });
    };

    // Rows are drawn by render() itself (not a theme menu), so touch is
    // hit-tested against the same rects (rowRect()).
    if (mappedInput.hasTouchHardware()) {
      const int start = pageStart();
      const int end = std::min(appCount, start + rowsPerPage());
      for (int i = start; i < end; ++i) {
        const Rect row = rowRect(i - start);
        if (mappedInput.wasTapInRect(row.x, row.y, row.width, row.height)) {
          selectedIndex = i;
          launchSelectedApp();
          return;
        }
      }
      const auto swipe = mappedInput.wasSwipe();
      if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
        const int perPage = rowsPerPage();
        selectedIndex = swipe == MappedInputManager::SwipeDir::Up ? std::min(appCount - 1, pageStart() + perPage)
                                                                  : std::max(0, pageStart() - perPage);
        requestUpdate();
        return;
      }
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
        mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      selectedIndex = (selectedIndex + 1) % appCount;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
               mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      selectedIndex = (selectedIndex - 1 + appCount) % appCount;
      requestUpdate();
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      launchSelectedApp();
    }
  }

  void render(RenderLock&& lock) override {
    renderer.clearScreen();

    const int pageWidth = renderer.getScreenWidth();
    const int pageHeight = renderer.getScreenHeight();
    const auto& metrics = UITheme::getInstance().getMetrics();

    const int headerY = metrics.topPadding;
    const int headerH = metrics.headerHeight;
    const int startY = headerY + headerH + metrics.verticalSpacing;
    const int menuHeight = pageHeight - startY - metrics.buttonHintsHeight - metrics.verticalSpacing;

    GUI.drawHeader(renderer, Rect{0, headerY, pageWidth, headerH}, tr(STR_APPS_TITLE));

    // One row per app: its own 32x32 icon (icon.raw in its folder, or a book
    // when it has none), name, and version/author. The selected row gets a
    // thick border rather than a fill, so icons never need inverting.
    const int appCount = static_cast<int>(visibleApps.size());
    const int start = pageStart();
    const int end = std::min(appCount, start + rowsPerPage());
    auto target = makeUiTarget(renderer);
    for (int i = start; i < end; ++i) {
      const AppEntry& app = visibleApps[i];
      const Rect row = rowRect(i - start);
      const bool selected = i == selectedIndex;
      renderer.drawRoundedRect(row.x, row.y, row.width, row.height, selected ? 3 : 1, 6, true);
      const int iconX = row.x + 12;
      const int iconY = row.y + (row.height - ICON_SIZE) / 2;
      if (app.hasIcon) {
        renderer.drawIcon(app.iconData, iconX, iconY, ICON_SIZE, ICON_SIZE);
      } else {
        const freeink::Icon& icon = app.builtIn ? (app.icon == UIIcon::Settings ? icon_cog_32 : icon_lyra_library_32)
                                                : icon_book_32;
        target.bitmap(freeink::ui::Rect{static_cast<int16_t>(iconX), static_cast<int16_t>(iconY),
                                        static_cast<int16_t>(ICON_SIZE), static_cast<int16_t>(ICON_SIZE)},
                      freeink::ui::bitmapFromIcon(icon), freeink::ui::BitmapMode::Center);
      }
      const int textX = iconX + ICON_SIZE + 14;
      const int textWidth = row.x + row.width - 12 - textX;
      const int nameHeight = renderer.getLineHeight(UI_12_FONT_ID);
      const int detailHeight = app.detail.empty() ? 0 : renderer.getLineHeight(SMALL_FONT_ID) + 2;
      const int nameY = row.y + (row.height - nameHeight - detailHeight) / 2;
      const std::string name = renderer.truncatedText(UI_12_FONT_ID, app.name.c_str(), textWidth, EpdFontFamily::BOLD);
      renderer.drawText(UI_12_FONT_ID, textX, nameY, name.c_str(), true, EpdFontFamily::BOLD);
      if (!app.detail.empty()) {
        const std::string detail = renderer.truncatedText(SMALL_FONT_ID, app.detail.c_str(), textWidth);
        renderer.drawText(SMALL_FONT_ID, textX, nameY + nameHeight + 2, detail.c_str(), true);
      }
    }

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

    renderer.displayBuffer();
  }

 private:
  struct AppEntry {
    std::string name;
    UIIcon icon;
    bool builtIn = false;  // App Store / App Settings rather than an SD-card app
    std::string dir;        // SD-card app: its folder and entry script
    std::string entry;
    bool hasIcon = false;
    uint8_t iconData[128] = {0};
    std::string detail;  // "v1.0.0  •  author" under the name; empty for built-ins
  };

  static constexpr int ICON_SIZE = 32;
  static constexpr int ROW_HEIGHT = 60;
  static constexpr int ROW_GAP = 8;
  static constexpr int ROW_MARGIN = 16;

  int listTop() const {
    const auto& metrics = UITheme::getInstance().getMetrics();
    return metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  }

  int rowsPerPage() const {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int available = renderer.getScreenHeight() - listTop() - metrics.buttonHintsHeight - metrics.verticalSpacing;
    return std::max(1, (available + ROW_GAP) / (ROW_HEIGHT + ROW_GAP));
  }

  int pageStart() const { return (selectedIndex / rowsPerPage()) * rowsPerPage(); }

  // Shared by render() (what gets drawn) and loop()'s touch hit-test.
  Rect rowRect(const int local) const {
    return Rect{ROW_MARGIN, listTop() + local * (ROW_HEIGHT + ROW_GAP), renderer.getScreenWidth() - 2 * ROW_MARGIN,
                ROW_HEIGHT};
  }

  int selectedIndex = 0;
  std::vector<AppEntry> visibleApps;

  // The list (icons, names, launchers) is rebuilt by refreshMenuList() when
  // the app returns, so it need not hold heap while the app runs.
  void releaseHeapWhileCovered() override {
    visibleApps.clear();
    visibleApps.shrink_to_fit();
  }

  std::unique_ptr<Activity> makeActivity(const AppEntry& app) {
    if (!app.builtIn) return std::make_unique<LuaAppActivity>(renderer, mappedInput, app.dir, app.name, app.entry);
    if (app.icon == UIIcon::Settings) return std::make_unique<AppSettingsActivity>(renderer, mappedInput);
    return std::make_unique<AppStoreActivity>(renderer, mappedInput);
  }

  // Rebuilt after every app, when an X3's heap may be short and fragmented:
  // one list sized up front, strings moved rather than copied.
  void refreshMenuList() {
    auto& settings = SmudgeSettings::getInstance();

    visibleApps.clear();
    visibleApps.shrink_to_fit();

    // Discover installed packages on the SD card (app_paths::SCAN_DIRS)
    auto installedPackages = AppPackage::scanApplications();
    visibleApps.reserve(installedPackages.size() + 2);
    for (auto& pkg : installedPackages) {
      AppEntry app;
      app.icon = UIIcon::Book;
      app.detail = "v" + pkg.version;
      if (!pkg.author.empty()) app.detail += "  \u00b7  " + pkg.author;
      app.name = std::move(pkg.name);
      app.dir = std::move(pkg.path);
      app.entry = std::move(pkg.entryScript);
      app.hasIcon = pkg.hasIcon;
      if (pkg.hasIcon) {
        std::memcpy(app.iconData, pkg.iconData, sizeof(pkg.iconData));
      }
      visibleApps.push_back(std::move(app));
    }
    installedPackages.clear();
    installedPackages.shrink_to_fit();

    std::vector<std::string> allNames;
    allNames.reserve(visibleApps.size());
    for (const auto& a : visibleApps) {
      allNames.push_back(a.name);
    }
    settings.registerKnownApps(allNames);

    if (settings.sortMode == MenuSortMode::Alphabetical) {
      std::sort(visibleApps.begin(), visibleApps.end(),
                [](const AppEntry& a, const AppEntry& b) { return a.name < b.name; });
    } else if (settings.sortMode == MenuSortMode::MostUsed) {
      std::sort(visibleApps.begin(), visibleApps.end(), [&](const AppEntry& a, const AppEntry& b) {
        auto itA = std::find_if(settings.apps.begin(), settings.apps.end(),
                                [&](const AppUsageData& d) { return d.appName == a.name; });
        auto itB = std::find_if(settings.apps.begin(), settings.apps.end(),
                                [&](const AppUsageData& d) { return d.appName == b.name; });
        float scoreA = (itA != settings.apps.end()) ? itA->usageScore : 0.0f;
        float scoreB = (itB != settings.apps.end()) ? itB->usageScore : 0.0f;
        return scoreA > scoreB;
      });
    }

    // Always append App Store and App Settings at the bottom
    AppEntry store;
    store.name = tr(STR_APPS_STORE);
    store.icon = UIIcon::Library;
    store.builtIn = true;
    visibleApps.push_back(std::move(store));
    AppEntry appSettings;
    appSettings.name = tr(STR_APPS_SETTINGS);
    appSettings.icon = UIIcon::Settings;
    appSettings.builtIn = true;
    visibleApps.push_back(std::move(appSettings));

    if (selectedIndex >= static_cast<int>(visibleApps.size())) {
      selectedIndex = std::max(0, static_cast<int>(visibleApps.size()) - 1);
    }
  }
};