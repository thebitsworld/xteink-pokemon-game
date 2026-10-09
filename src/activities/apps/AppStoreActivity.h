#pragma once

#include <ArduinoJson.h>
#include <HalStorage.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "Logging.h"
#include "SdCardFontSystem.h"
#include "activities/Activity.h"
#include "activities/apps/lua/AppPaths.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"

#if !defined(SIMULATOR)
#include <WiFi.h>
#endif

#ifndef CROSSINK_APP_STORE_URL
#define CROSSINK_APP_STORE_URL "https://raw.githubusercontent.com/thebitsworld/xteink-pokemon-game/main/apps/"
#endif

class AppStoreActivity : public Activity {
 public:
  AppStoreActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("App Store", renderer, mappedInput) {}

  void onEnter() override {
    Activity::onEnter();
    sdFontSystem.releaseForNetwork(renderer);
    // This repo's own apps/ folder (catalog.json + one folder per app).
    catalogUrl_ = std::string(CROSSINK_APP_STORE_URL) + "catalog.json";
    baseUrl_ = CROSSINK_APP_STORE_URL;
    selectedIndex_ = 0;
    errorTitle_ = tr(STR_APPS_NETWORK_ERROR);
    errorMessage_.clear();
    lastFailedAppIndex_ = -1;
    if (isWifiConnected()) {
      state_ = State::FETCHING_CATALOG;
    } else {
      state_ = State::CHECK_WIFI;
    }
    requestUpdate();
  }

  void onExit() override {
    apps_.clear();
    apps_.shrink_to_fit();
    sdFontSystem.ensureLoaded(renderer);
    sdFontSystem.releaseRegistry();
    Activity::onExit();
  }

  void render(RenderLock&&) override {
    switch (state_) {
      case State::CHECK_WIFI:
        if (!isWifiConnected()) {
          renderNoWifi();
        } else {
          renderLoading(tr(STR_APPS_CONNECTING_CATALOG));
        }
        break;

      case State::FETCHING_CATALOG:
        renderLoading(tr(STR_APPS_FETCHING_CATALOG));
        break;

      case State::CATALOG_READY:
        renderCatalog();
        break;

      case State::APP_DETAIL:
        renderAppDetail();
        break;

      case State::DOWNLOADING: {
        const char* appName = (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(apps_.size()))
                                  ? apps_[selectedIndex_].name.c_str()
                                  : "";
        renderDownloadProgress(appName);
        break;
      }

      case State::ERROR:
        renderError();
        break;
    }
    renderer.displayBuffer();
  }

  void loop() override {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      if (state_ == State::DOWNLOADING) {
        cancelDownload_ = true;
        return;
      }
      if (state_ == State::APP_DETAIL) {
        state_ = State::CATALOG_READY;
        requestUpdate();
        return;
      }
      if (state_ == State::ERROR) {
        if (!apps_.empty()) {
          state_ = State::CATALOG_READY;
          requestUpdate();
          return;
        }
      }
      finish();
      return;
    }

    switch (state_) {
      case State::CHECK_WIFI: {
        if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
          openWifiSelection();
          return;
        }
        int tx = 0, ty = 0;
        if (mappedInput.wasScreenTapped(tx, ty)) {
          int w = renderer.getScreenWidth();
          int h = renderer.getScreenHeight();
          const auto& m = UITheme::getInstance().getMetrics();
          if (actionButtonHit(tx, ty)) {
            openWifiSelection();
            return;
          }
          if (ty > h - m.buttonHintsHeight) {
            if (tx < w / 2) {
              finish();
              return;
            } else {
              openWifiSelection();
              return;
            }
          }
        }
        if (isWifiConnected()) {
          state_ = State::FETCHING_CATALOG;
          requestUpdateAndWait();
          fetchCatalog();
        }
        break;
      }

      case State::FETCHING_CATALOG:
        fetchCatalog();
        break;

      case State::CATALOG_READY:
        handleCatalogInput();
        break;

      case State::APP_DETAIL:
        handleDetailInput();
        break;

      case State::DOWNLOADING:
        // Handled during download operation
        break;

      case State::ERROR: {
        if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
          retry();
          return;
        }
        int tx = 0, ty = 0;
        if (mappedInput.wasScreenTapped(tx, ty)) {
          int w = renderer.getScreenWidth();
          int h = renderer.getScreenHeight();
          const auto& m = UITheme::getInstance().getMetrics();
          if (actionButtonHit(tx, ty)) {
            retry();
            return;
          }
          if (ty > h - m.buttonHintsHeight) {
            if (tx < w / 2) {
              if (!apps_.empty()) {
                state_ = State::CATALOG_READY;
                requestUpdate();
              } else {
                finish();
              }
              return;
            } else {
              retry();
              return;
            }
          }
        }
        break;
      }
    }
  }

 private:
  void openWifiSelection() {
    startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                           [this](const ActivityResult&) {
                             if (isWifiConnected()) {
                               state_ = State::FETCHING_CATALOG;
                               requestUpdateAndWait();
                               fetchCatalog();
                             } else {
                               state_ = State::CHECK_WIFI;
                               requestUpdate();
                             }
                           });
  }

  // Retries what failed: the install, or else the catalog (after Wi-Fi when
  // it dropped).
  void retry() {
    if (!isWifiConnected()) {
      state_ = State::CHECK_WIFI;
      requestUpdate();
    } else if (lastFailedAppIndex_ >= 0 && lastFailedAppIndex_ < static_cast<int>(apps_.size())) {
      installApp(apps_[lastFailedAppIndex_]);
    } else {
      state_ = State::FETCHING_CATALOG;
      requestUpdateAndWait();
      fetchCatalog();
    }
  }

  // The on-screen button of the no-Wi-Fi and error screens. Touch-only
  // readers draw no button hints, so without it they had nothing to tap.
  Rect actionButtonRect() const {
    const int w = renderer.getScreenWidth();
    constexpr int bw = 320;
    return Rect{(w - bw) / 2, renderer.getScreenHeight() / 2 + 110, bw, 50};
  }

  bool actionButtonHit(const int x, const int y) const {
    const Rect b = actionButtonRect();
    return x >= b.x && x < b.x + b.width && y >= b.y && y < b.y + b.height;
  }

  void drawActionButton(const char* label) {
    const Rect b = actionButtonRect();
    renderer.fillRoundedRect(b.x, b.y, b.width, b.height, 8, Color::Black);
    const int tw = renderer.getTextWidth(UI_12_FONT_ID, label, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, b.x + (b.width - tw) / 2, b.y + 14, label, false, EpdFontFamily::BOLD);
  }

  enum class State { CHECK_WIFI, FETCHING_CATALOG, CATALOG_READY, APP_DETAIL, DOWNLOADING, ERROR };

  struct CatalogApp {
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    std::vector<std::string> files;
    bool isInstalled = false;
    std::string installedVersion;
    bool hasIcon = false;
    uint8_t iconData[128] = {0};
  };

  State state_ = State::CHECK_WIFI;
  std::string catalogUrl_;
  std::string baseUrl_;
  std::string errorTitle_;
  std::string errorMessage_;
  int lastFailedAppIndex_ = -1;
  std::vector<CatalogApp> apps_;
  int selectedIndex_ = 0;
  bool cancelDownload_ = false;

  std::string currentDownloadingFile_;
  int currentFileNum_ = 0;
  int totalFiles_ = 0;

  static void expand32To64(const uint8_t src[128], uint8_t dst[512]) {
    for (int y = 0; y < 32; ++y) {
      int srcRowOffset = y * 4;
      int dstRow0 = (y * 2) * 8;
      int dstRow1 = (y * 2 + 1) * 8;
      for (int xByte = 0; xByte < 4; ++xByte) {
        uint8_t b = src[srcRowOffset + xByte];
        uint8_t hi = 0;
        uint8_t lo = 0;
        for (int bit = 0; bit < 4; ++bit) {
          if ((b >> (7 - bit)) & 1) {
            hi |= (3 << (6 - bit * 2));
          }
          if ((b >> (3 - bit)) & 1) {
            lo |= (3 << (6 - bit * 2));
          }
        }
        dst[dstRow0 + xByte * 2] = hi;
        dst[dstRow0 + xByte * 2 + 1] = lo;
        dst[dstRow1 + xByte * 2] = hi;
        dst[dstRow1 + xByte * 2 + 1] = lo;
      }
    }
  }

  bool isWifiConnected() {
#if defined(SIMULATOR)
    return true;
#else
    return (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0));
#endif
  }

  void loadAppIcon(CatalogApp& app, bool allowDownload = false) {
    app.hasIcon = false;
    const std::string installedDir = app_paths::findAppDir(app.id);
    const std::string iconPath = installedDir.empty() ? "" : installedDir + "/icon.raw";
    const std::string cachePath = std::string(app_paths::CACHE_DIR) + "/icons/" + app.id + ".raw";

    HalFile f;
    if ((!iconPath.empty() && Storage.openFileForRead("STORE", iconPath.c_str(), f)) ||
        Storage.openFileForRead("STORE", cachePath.c_str(), f)) {
      size_t readBytes = f.read(app.iconData, sizeof(app.iconData));
      f.close();
      if (readBytes == sizeof(app.iconData)) {
        app.hasIcon = true;
        return;
      }
    }

#if defined(SIMULATOR)
    std::string hostPath = "apps/" + app.id + "/icon.raw";
    FILE* hf = fopen(hostPath.c_str(), "rb");
    if (!hf) hf = fopen(("../" + hostPath).c_str(), "rb");
    if (hf) {
      size_t r = fread(app.iconData, 1, sizeof(app.iconData), hf);
      fclose(hf);
      if (r == sizeof(app.iconData)) {
        app.hasIcon = true;
        return;
      }
    }
#endif

    if (allowDownload && isWifiConnected()) {
      Storage.ensureDirectoryExists((std::string(app_paths::CACHE_DIR) + "/icons").c_str());
      std::string iconUrl = baseUrl_ + app.id + "/icon.raw";
      HttpDownloader::DownloadOptions options;
#if defined(FREEINK_NET_WOLFSSL)
      options.transport = HttpDownloader::Transport::WOLFSSL;
#endif
      if (HttpDownloader::downloadToFile(iconUrl, cachePath, nullptr, nullptr, "", "", options) == HttpDownloader::OK) {
        if (Storage.openFileForRead("STORE", cachePath.c_str(), f)) {
          size_t readBytes = f.read(app.iconData, sizeof(app.iconData));
          f.close();
          if (readBytes == sizeof(app.iconData)) {
            app.hasIcon = true;
          }
        }
      }
    }
  }

  void ensureAppIcon(CatalogApp& app) {
    if (app.hasIcon) return;
    loadAppIcon(app, true);
  }

  void fetchCatalog() {
    sdFontSystem.releaseForNetwork(renderer);

    std::string tmpCatalog = std::string(app_paths::CACHE_DIR) + "/catalog.tmp";
    Storage.ensureDirectoryExists(app_paths::CACHE_DIR);
    Storage.remove(tmpCatalog.c_str());

    HttpDownloader::DownloadOptions options;
#if defined(FREEINK_NET_WOLFSSL)
    options.transport = HttpDownloader::Transport::WOLFSSL;
#endif

    HttpDownloader::DownloadError err =
        HttpDownloader::downloadToFile(catalogUrl_, tmpCatalog, nullptr, nullptr, "", "", options);

#if defined(FREEINK_NET_WOLFSSL)
    if (err != HttpDownloader::OK) {
      LOG_DBG("STORE", "WolfSSL catalog fetch failed (%d), trying ESP_HTTP", err);
      options.transport = HttpDownloader::Transport::ESP_HTTP;
      err = HttpDownloader::downloadToFile(catalogUrl_, tmpCatalog, nullptr, nullptr, "", "", options);
    }
#endif

#if defined(SIMULATOR)
    if (err != HttpDownloader::OK) {
      FILE* f = fopen("apps/catalog.json", "rb");
      if (!f) f = fopen("../apps/catalog.json", "rb");
      if (f) {
        HalFile outF;
        if (Storage.openFileForWrite("STORE", tmpCatalog.c_str(), outF)) {
          uint8_t buf[256];
          size_t bytes;
          while ((bytes = fread(buf, 1, sizeof(buf), f)) > 0) {
            outF.write(buf, bytes);
          }
          outF.close();
          err = HttpDownloader::OK;
        }
        fclose(f);
      }
    }
#endif

    if (err != HttpDownloader::OK) {
      Storage.remove(tmpCatalog.c_str());
      state_ = State::ERROR;
      lastFailedAppIndex_ = -1;
      errorTitle_ = tr(STR_APPS_NETWORK_ERROR);
      errorMessage_ = tr(STR_APPS_ERR_CATALOG_DOWNLOAD);
      requestUpdate();
      return;
    }

    FsFile catalogFile;
    if (!Storage.openFileForRead("STORE", tmpCatalog.c_str(), catalogFile)) {
      Storage.remove(tmpCatalog.c_str());
      state_ = State::ERROR;
      lastFailedAppIndex_ = -1;
      errorTitle_ = tr(STR_APPS_STORAGE_ERROR);
      errorMessage_ = tr(STR_APPS_ERR_CATALOG_READ);
      requestUpdate();
      return;
    }

    JsonDocument doc;
    DeserializationError jsonErr = deserializeJson(doc, catalogFile);
    catalogFile.close();
    Storage.remove(tmpCatalog.c_str());

    if (jsonErr) {
      state_ = State::ERROR;
      lastFailedAppIndex_ = -1;
      errorTitle_ = tr(STR_APPS_CATALOG_ERROR);
      errorMessage_ = formatText(tr(STR_APPS_ERR_JSON), jsonErr.c_str());
      requestUpdate();
      return;
    }

    apps_.clear();
    JsonArray appList = doc["apps"].as<JsonArray>();
    for (JsonObject obj : appList) {
      CatalogApp app;
      app.id = obj["id"] | "";
      app.name = obj["name"] | app.id.c_str();
      app.version = obj["version"] | "1.0.0";
      app.author = obj["author"] | "Unknown";
      app.description = obj["description"] | "";

      JsonArray files = obj["files"].as<JsonArray>();
      for (const char* f : files) {
        if (f) app.files.push_back(f);
      }
      if (app.files.empty()) {
        app.files = {"manifest.json", "main.lua", "icon.raw"};
      }

      checkInstalledStatus(app);
      loadAppIcon(app, false);
      apps_.push_back(std::move(app));
    }

    state_ = State::CATALOG_READY;
    selectedIndex_ = 0;
    requestUpdate();
  }

  void checkInstalledStatus(CatalogApp& app) {
    const std::string installedDir = app_paths::findAppDir(app.id);
    std::string manifestPath;
    if (!installedDir.empty() && Storage.exists((installedDir + "/manifest.json").c_str())) {
      manifestPath = installedDir + "/manifest.json";
    }

    if (!manifestPath.empty()) {
      String mStr = Storage.readFile(manifestPath.c_str());
      JsonDocument mDoc;
      if (!deserializeJson(mDoc, mStr)) {
        app.isInstalled = true;
        app.installedVersion = mDoc["version"] | "1.0.0";
        return;
      }
    }
    app.isInstalled = false;
    app.installedVersion = "";
  }

  void handleCatalogInput() {
    int count = static_cast<int>(apps_.size());
    if (count == 0) return;

    const int rowsPerPage = 8;
    int pageStart = (selectedIndex_ / rowsPerPage) * rowsPerPage;

    if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
        mappedInput.wasReleased(MappedInputManager::Button::Down) ||
        mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
      selectedIndex_ = (selectedIndex_ + 1) % count;
      requestUpdate();
      return;
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
               mappedInput.wasReleased(MappedInputManager::Button::Up) ||
               mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
      selectedIndex_ = (selectedIndex_ - 1 + count) % count;
      requestUpdate();
      return;
    }

    auto swipe = mappedInput.wasSwipe();
    if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Left) {
      selectedIndex_ = (selectedIndex_ + 1) % count;
      requestUpdate();
      return;
    } else if (swipe == MappedInputManager::SwipeDir::Down || swipe == MappedInputManager::SwipeDir::Right) {
      selectedIndex_ = (selectedIndex_ - 1 + count) % count;
      requestUpdate();
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      state_ = State::APP_DETAIL;
      ensureAppIcon(apps_[selectedIndex_]);
      requestUpdate();
      return;
    }

    // Touch handling
    int tx = 0, ty = 0;
    if (mappedInput.wasScreenTapped(tx, ty)) {
      int w = renderer.getScreenWidth();
      int h = renderer.getScreenHeight();
      const auto& m = UITheme::getInstance().getMetrics();

      if (ty > h - m.buttonHintsHeight - 20) {
        if (tx < w / 4) {
          finish();
          return;
        } else if (tx < w / 2) {
          state_ = State::APP_DETAIL;
          ensureAppIcon(apps_[selectedIndex_]);
          requestUpdate();
          return;
        } else if (tx < 3 * w / 4) {
          selectedIndex_ = (selectedIndex_ - 1 + count) % count;
          requestUpdate();
          return;
        } else {
          selectedIndex_ = (selectedIndex_ + 1) % count;
          requestUpdate();
          return;
        }
      }

      int startY = m.topPadding + m.headerHeight + 6;
      int rowH = 56;
      for (int i = pageStart; i < count && i < pageStart + rowsPerPage; ++i) {
        int ry = startY + (i - pageStart) * rowH;
        if (tx >= 16 && tx <= w - 16 && ty >= ry && ty <= ry + rowH - 4) {
          if (selectedIndex_ == i) {
            state_ = State::APP_DETAIL;
            ensureAppIcon(apps_[selectedIndex_]);
          } else {
            selectedIndex_ = i;
          }
          requestUpdate();
          return;
        }
      }
    }
  }

  int getDetailActionButtonY() const {
    const auto& m = UITheme::getInstance().getMetrics();
    int cardY = m.topPadding + m.headerHeight + 12;
    int cardH = 96;
    int descY = cardY + cardH + 12;
    int descH = 176;
    int pkgY = descY + descH + 12;
    int pkgH = 118;
    return pkgY + pkgH + 16;
  }

  void handleDetailInput() {
    int count = static_cast<int>(apps_.size());
    if (count == 0 || selectedIndex_ < 0 || selectedIndex_ >= count) {
      state_ = State::CATALOG_READY;
      requestUpdate();
      return;
    }

    auto& app = apps_[selectedIndex_];

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state_ = State::CATALOG_READY;
      requestUpdate();
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      installApp(app);
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      if (app.isInstalled) {
        uninstallApp(app);
        return;
      }
    }

    // Touch handling
    int tx = 0, ty = 0;
    if (mappedInput.wasScreenTapped(tx, ty)) {
      int w = renderer.getScreenWidth();
      int h = renderer.getScreenHeight();
      const auto& m = UITheme::getInstance().getMetrics();

      if (ty > h - m.buttonHintsHeight - 20) {
        if (tx < w / 4) {
          state_ = State::CATALOG_READY;
          requestUpdate();
          return;
        } else if (tx < w / 2) {
          installApp(app);
          return;
        } else if (tx < 3 * w / 4 && app.isInstalled) {
          uninstallApp(app);
          return;
        }
      }

      int btnY = getDetailActionButtonY();
      int btnH = 50;
      if (ty >= btnY && ty <= btnY + btnH) {
        if (!app.isInstalled) {
          if (tx >= 40 && tx <= w - 40) {
            installApp(app);
            return;
          }
        } else {
          int bw = 200;
          int bx1 = 24;
          int bx2 = w - 24 - bw;
          if (tx >= bx1 && tx <= bx1 + bw) {
            installApp(app);
            return;
          } else if (tx >= bx2 && tx <= bx2 + bw) {
            uninstallApp(app);
            return;
          }
        }
      }
    }
  }

  void installApp(CatalogApp& app) {
    if (!isWifiConnected()) {
      state_ = State::ERROR;
      lastFailedAppIndex_ = selectedIndex_;
      errorTitle_ = tr(STR_APPS_NETWORK_ERROR);
      errorMessage_ = tr(STR_APPS_ERR_WIFI_LOST);
      requestUpdate();
      return;
    }

    state_ = State::DOWNLOADING;
    cancelDownload_ = false;
    totalFiles_ = static_cast<int>(app.files.size());
    lastFailedAppIndex_ = selectedIndex_;

    sdFontSystem.releaseForNetwork(renderer);

    std::string targetDir = app_paths::installDirFor(app.id);
    bool dirOk = Storage.ensureDirectoryExists(targetDir.c_str());
    if (!dirOk) {
      LOG_ERR("STORE", "Failed to create target directory: %s", targetDir.c_str());
      state_ = State::ERROR;
      errorTitle_ = tr(STR_APPS_STORAGE_ERROR);
      errorMessage_ = tr(STR_APPS_ERR_APP_FOLDER);
      requestUpdate();
      return;
    }

    for (int i = 0; i < totalFiles_; ++i) {
      if (cancelDownload_) {
        LOG_INF("STORE", "Installation cancelled");
        break;
      }
      currentFileNum_ = i + 1;
      currentDownloadingFile_ = app.files[i];
      requestUpdateAndWait();

      std::string destPath = targetDir + "/" + app.files[i];

      // Ensure any subdirectories exist
      size_t lastSlash = destPath.find_last_of('/');
      if (lastSlash != std::string::npos) {
        std::string parentDir = destPath.substr(0, lastSlash);
        if (!Storage.ensureDirectoryExists(parentDir.c_str())) {
          LOG_ERR("STORE", "Failed to create parent directory: %s", parentDir.c_str());
          state_ = State::ERROR;
          errorTitle_ = tr(STR_APPS_STORAGE_ERROR);
          errorMessage_ = formatText(tr(STR_APPS_ERR_FOLDER), parentDir.c_str());
          requestUpdate();
          return;
        }
      }

      if (Storage.exists(destPath.c_str())) {
        Storage.remove(destPath.c_str());
      }

      std::string fileUrl = baseUrl_ + app.id + "/" + app.files[i];

      HttpDownloader::DownloadOptions options;
      options.shouldCancel = [this]() { return cancelDownload_; };
#if defined(FREEINK_NET_WOLFSSL)
      options.transport = HttpDownloader::Transport::WOLFSSL;
#endif

      HttpDownloader::DownloadError err = HttpDownloader::HTTP_ERROR;

#if defined(SIMULATOR)
      std::string hostSrc = "apps/" + app.id + "/" + app.files[i];
      FILE* inHost = fopen(hostSrc.c_str(), "rb");
      if (!inHost) inHost = fopen(("../" + hostSrc).c_str(), "rb");
      if (inHost) {
        HalFile outF;
        if (Storage.openFileForWrite("STORE", destPath.c_str(), outF)) {
          uint8_t buf[256];
          size_t bytes;
          while ((bytes = fread(buf, 1, sizeof(buf), inHost)) > 0) {
            outF.write(buf, bytes);
          }
          outF.close();
          err = HttpDownloader::OK;
        } else {
          err = HttpDownloader::FILE_ERROR;
        }
        fclose(inHost);
      }
      if (err != HttpDownloader::OK) {
        err = HttpDownloader::downloadToFile(fileUrl, destPath, nullptr, &cancelDownload_, "", "", options);
      }
#else
      for (int attempt = 0; attempt < 3 && err != HttpDownloader::OK && !cancelDownload_; ++attempt) {
        if (attempt > 0) {
          delay(500);
        }
#if defined(FREEINK_NET_WOLFSSL)
        options.transport = (attempt == 1) ? HttpDownloader::Transport::ESP_HTTP : HttpDownloader::Transport::WOLFSSL;
#endif
        err = HttpDownloader::downloadToFile(fileUrl, destPath, nullptr, &cancelDownload_, "", "", options);
        if (err == HttpDownloader::FILE_ERROR) {
          break;
        }
      }
#endif

      if (err != HttpDownloader::OK) {
        if (cancelDownload_) {
          LOG_INF("STORE", "Download cancelled");
          break;
        }
        LOG_ERR("STORE", "Failed downloading %s: %d", app.files[i].c_str(), err);
        state_ = State::ERROR;
        if (err == HttpDownloader::FILE_ERROR) {
          errorTitle_ = tr(STR_APPS_STORAGE_ERROR);
          errorMessage_ = formatText(tr(STR_APPS_ERR_WRITE), app.files[i].c_str());
        } else {
          errorTitle_ = tr(STR_APPS_NETWORK_ERROR);
          if (!isWifiConnected()) {
            errorMessage_ = tr(STR_APPS_ERR_WIFI_LOST);
          } else {
            errorMessage_ = formatText(tr(STR_APPS_ERR_DOWNLOAD), app.files[i].c_str());
          }
        }
        requestUpdate();
        return;
      }
    }

    checkInstalledStatus(app);
    loadAppIcon(app, false);
    state_ = State::APP_DETAIL;
    requestUpdate();
  }

  void uninstallApp(CatalogApp& app) {
    // Remove every copy, wherever it was installed or copied by hand.
    for (std::string dir = app_paths::findAppDir(app.id); !dir.empty(); dir = app_paths::findAppDir(app.id)) {
      if (!Storage.removeDir(dir.c_str())) break;
    }

    checkInstalledStatus(app);
    state_ = State::APP_DETAIL;
    requestUpdate();
  }

  // printf-style formatting of a translated message with one string argument.
  static std::string formatText(const char* format, const char* arg) {
    char buffer[192];
    snprintf(buffer, sizeof(buffer), format, arg);
    return buffer;
  }

  void renderLoading(const char* msg) {
    int w = renderer.getScreenWidth();
    int h = renderer.getScreenHeight();
    const auto& m = UITheme::getInstance().getMetrics();

    renderer.clearScreen();
    GUI.drawHeader(renderer, Rect{0, m.topPadding, w, m.headerHeight}, tr(STR_APPS_STORE), tr(STR_APPS_CONNECTING));
    renderer.drawCenteredText(UI_12_FONT_ID, h / 2 - 20, msg, true, EpdFontFamily::BOLD);

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  void renderNoWifi() {
    int w = renderer.getScreenWidth();
    int h = renderer.getScreenHeight();
    const auto& m = UITheme::getInstance().getMetrics();

    renderer.clearScreen();
    GUI.drawHeader(renderer, Rect{0, m.topPadding, w, m.headerHeight}, tr(STR_APPS_STORE), tr(STR_APPS_OFFLINE));

    renderer.drawCenteredText(UI_12_FONT_ID, h / 2 - 40, tr(STR_APPS_WIFI_NOT_CONNECTED), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(SMALL_FONT_ID, h / 2 - 10, tr(STR_APPS_CONNECT_WIFI_HINT), true);

    drawActionButton(tr(STR_APPS_CONNECT_WIFI));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_APPS_CONNECT_WIFI), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  void renderCatalog() {
    int w = renderer.getScreenWidth();
    int h = renderer.getScreenHeight();
    const auto& m = UITheme::getInstance().getMetrics();

    renderer.clearScreen();
    char subBuf[64];
    snprintf(subBuf, sizeof(subBuf), tr(STR_APPS_AVAILABLE), static_cast<unsigned>(apps_.size()));
    GUI.drawHeader(renderer, Rect{0, m.topPadding, w, m.headerHeight}, tr(STR_APPS_STORE), subBuf);

    int startY = m.topPadding + m.headerHeight + 6;
    int rowH = 56;
    int count = static_cast<int>(apps_.size());
    const int rowsPerPage = 8;
    int pageStart = (selectedIndex_ / rowsPerPage) * rowsPerPage;

    for (int i = pageStart; i < count && i < pageStart + rowsPerPage; ++i) {
      const auto& app = apps_[i];
      int y = startY + (i - pageStart) * rowH;
      bool isSelected = (i == selectedIndex_);

      if (isSelected) {
        renderer.fillRoundedRect(16, y, w - 32, rowH - 4, 6, Color::Black);
      } else {
        renderer.drawRoundedRect(16, y, w - 32, rowH - 4, 1, 6, true);
      }

      // 32x32 App Icon
      int iconX = 26;
      int iconY = y + 8;
      if (app.hasIcon) {
        if (isSelected) {
          renderer.drawIconInverted(app.iconData, iconX, iconY, 32, 32);
        } else {
          renderer.drawIcon(app.iconData, iconX, iconY, 32, 32);
        }
      } else {
        renderer.drawRoundedRect(iconX, iconY, 32, 32, 1, 4, !isSelected);
      }

      // App Name
      char titleBuf[64];
      snprintf(titleBuf, sizeof(titleBuf), "%s", app.name.c_str());
      renderer.drawText(UI_10_FONT_ID, 68, y + 8, titleBuf, !isSelected, EpdFontFamily::BOLD);

      // Author & Version
      char authBuf[96];
      snprintf(authBuf, sizeof(authBuf), tr(STR_APPS_AUTHOR_VERSION), app.author.c_str(), app.version.c_str());
      renderer.drawText(SMALL_FONT_ID, 68, y + 28, authBuf, !isSelected);

      // Status badge on the right
      const char* status = !app.isInstalled                    ? tr(STR_APPS_INSTALL)
                           : app.installedVersion != app.version ? tr(STR_APPS_UPDATE)
                                                                 : tr(STR_APPS_INSTALLED);
      char statusStr[48];
      snprintf(statusStr, sizeof(statusStr), "[ %s ]", status);
      int sw = renderer.getTextWidth(SMALL_FONT_ID, statusStr, EpdFontFamily::BOLD);
      renderer.drawText(SMALL_FONT_ID, w - 32 - sw - 10, y + 18, statusStr, !isSelected, EpdFontFamily::BOLD);
    }

    // Page indicator and prompt below list
    int totalPages = std::max(1, (count + rowsPerPage - 1) / rowsPerPage);
    int curPage = (selectedIndex_ / rowsPerPage) + 1;
    char pageBuf[96];
    snprintf(pageBuf, sizeof(pageBuf), tr(STR_APPS_PAGE), curPage, totalPages, count);
    int footY = startY + (rowsPerPage * rowH) + 12;
    renderer.drawCenteredText(SMALL_FONT_ID, footY, pageBuf, true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(SMALL_FONT_ID, footY + 18, tr(STR_APPS_TAP_DETAILS), true);

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_APPS_DETAILS), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  void renderAppDetail() {
    int w = renderer.getScreenWidth();
    int h = renderer.getScreenHeight();
    const auto& m = UITheme::getInstance().getMetrics();

    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(apps_.size())) {
      state_ = State::CATALOG_READY;
      renderCatalog();
      return;
    }

    const auto& app = apps_[selectedIndex_];

    renderer.clearScreen();
    GUI.drawHeader(renderer, Rect{0, m.topPadding, w, m.headerHeight}, tr(STR_APPS_STORE), app.name.c_str());

    // 1. Hero Card at top
    int cardY = m.topPadding + m.headerHeight + 12;
    int cardH = 96;
    renderer.drawRoundedRect(16, cardY, w - 32, cardH, 1, 8, true);

    // Large 64x64 Hero Icon
    int iconFrameX = 28;
    int iconFrameY = cardY + 10;
    renderer.drawRoundedRect(iconFrameX, iconFrameY, 76, 76, 1, 6, true);
    if (app.hasIcon) {
      uint8_t scaled[512];
      expand32To64(app.iconData, scaled);
      renderer.drawIcon(scaled, iconFrameX + 6, iconFrameY + 6, 64, 64);
    } else {
      renderer.drawCenteredText(SMALL_FONT_ID, iconFrameY + 28, tr(STR_APPS_NO_ICON), true);
    }

    // App Details beside Hero Icon
    int infoX = 116;
    renderer.drawText(UI_12_FONT_ID, infoX, cardY + 14, app.name.c_str(), true, EpdFontFamily::BOLD);

    char devBuf[96];
    snprintf(devBuf, sizeof(devBuf), tr(STR_APPS_DEVELOPER), app.author.c_str());
    renderer.drawText(SMALL_FONT_ID, infoX, cardY + 40, devBuf, true);

    char verBuf[128];
    if (app.isInstalled) {
      if (app.installedVersion != app.version) {
        snprintf(verBuf, sizeof(verBuf), tr(STR_APPS_VERSION_UPGRADE), app.installedVersion.c_str(),
                 app.version.c_str());
      } else {
        snprintf(verBuf, sizeof(verBuf), tr(STR_APPS_VERSION_CURRENT), app.version.c_str());
      }
    } else {
      snprintf(verBuf, sizeof(verBuf), tr(STR_APPS_VERSION_LATEST), app.version.c_str());
    }
    renderer.drawText(SMALL_FONT_ID, infoX, cardY + 60, verBuf, true, EpdFontFamily::BOLD);

    // 2. About / Description Box
    int descY = cardY + cardH + 12;
    int descH = 176;
    renderer.drawRoundedRect(16, descY, w - 32, descH, 1, 8, true);
    renderer.drawText(UI_10_FONT_ID, 28, descY + 12, tr(STR_APPS_ABOUT), true, EpdFontFamily::BOLD);
    renderer.drawLine(28, descY + 34, w - 28, descY + 34, Color::Black);

    auto dLines = renderer.wrappedText(SMALL_FONT_ID, app.description.c_str(), w - 56, 5);
    int dy = descY + 44;
    for (const auto& dl : dLines) {
      renderer.drawText(SMALL_FONT_ID, 28, dy, dl.c_str(), true);
      dy += renderer.getLineHeight(SMALL_FONT_ID) + 4;
    }

    // 3. Package Information Box
    int pkgY = descY + descH + 12;
    int pkgH = 118;
    renderer.drawRoundedRect(16, pkgY, w - 32, pkgH, 1, 8, true);
    renderer.drawText(UI_10_FONT_ID, 28, pkgY + 12, tr(STR_APPS_PACKAGE_DETAILS), true, EpdFontFamily::BOLD);
    renderer.drawLine(28, pkgY + 34, w - 28, pkgY + 34, Color::Black);

    char pkgIdBuf[96];
    snprintf(pkgIdBuf, sizeof(pkgIdBuf), tr(STR_APPS_PACKAGE_ID), app.id.c_str());
    renderer.drawText(SMALL_FONT_ID, 28, pkgY + 44, pkgIdBuf, true);

    char pkgFilesBuf[96];
    snprintf(pkgFilesBuf, sizeof(pkgFilesBuf), tr(STR_APPS_PACKAGE_FILES), static_cast<unsigned>(app.files.size()));
    renderer.drawText(SMALL_FONT_ID, 28, pkgY + 64, pkgFilesBuf, true);

    const std::string pathBuf = formatText(tr(STR_APPS_LOCATION), app_paths::installDirFor(app.id).c_str());
    renderer.drawText(SMALL_FONT_ID, 28, pkgY + 84, pathBuf.c_str(), true);

    // 4. Action Buttons (Touch + Physical Prompts)
    int btnY = getDetailActionButtonY();
    int btnH = 50;

    if (!app.isInstalled) {
      // Single wide Install button
      int bw = 320;
      int bx = (w - bw) / 2;
      renderer.fillRoundedRect(bx, btnY, bw, btnH, 8, Color::Black);
      int tw = renderer.getTextWidth(UI_12_FONT_ID, tr(STR_APPS_INSTALL_APP), EpdFontFamily::BOLD);
      renderer.drawText(UI_12_FONT_ID, bx + (bw - tw) / 2, btnY + 14, tr(STR_APPS_INSTALL_APP), false,
                        EpdFontFamily::BOLD);
    } else {
      // Upgrade or Reinstall (Left) + Uninstall (Right)
      int bw = 200;
      int bx1 = 24;
      int bx2 = w - 24 - bw;
      const char* primaryText = (app.installedVersion != app.version) ? tr(STR_APPS_UPGRADE_APP) : tr(STR_APPS_REINSTALL);

      renderer.fillRoundedRect(bx1, btnY, bw, btnH, 8, Color::Black);
      int tw1 = renderer.getTextWidth(UI_12_FONT_ID, primaryText, EpdFontFamily::BOLD);
      renderer.drawText(UI_12_FONT_ID, bx1 + (bw - tw1) / 2, btnY + 14, primaryText, false, EpdFontFamily::BOLD);

      renderer.drawRoundedRect(bx2, btnY, bw, btnH, 2, 8, true);
      int tw2 = renderer.getTextWidth(UI_12_FONT_ID, tr(STR_APPS_UNINSTALL), EpdFontFamily::BOLD);
      renderer.drawText(UI_12_FONT_ID, bx2 + (bw - tw2) / 2, btnY + 14, tr(STR_APPS_UNINSTALL), true,
                        EpdFontFamily::BOLD);
    }

    // Button hints
    const char* hint1 = tr(STR_BACK);
    const char* hint2 = !app.isInstalled ? tr(STR_APPS_INSTALL)
                        : (app.installedVersion != app.version ? tr(STR_APPS_UPGRADE) : tr(STR_APPS_REINSTALL));
    const char* hint3 = app.isInstalled ? tr(STR_APPS_UNINSTALL) : "";
    const auto labels = mappedInput.mapLabels(hint1, hint2, hint3, "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  void renderDownloadProgress(const char* appName) {
    int w = renderer.getScreenWidth();
    int h = renderer.getScreenHeight();
    const auto& m = UITheme::getInstance().getMetrics();

    renderer.clearScreen();
    GUI.drawHeader(renderer, Rect{0, m.topPadding, w, m.headerHeight}, tr(STR_APPS_INSTALLING), appName);

    renderer.drawCenteredText(UI_12_FONT_ID, h / 2 - 40, tr(STR_APPS_DOWNLOADING), true, EpdFontFamily::BOLD);

    char fileBuf[128];
    snprintf(fileBuf, sizeof(fileBuf), tr(STR_APPS_FILE_PROGRESS), currentFileNum_, totalFiles_,
             currentDownloadingFile_.c_str());
    renderer.drawCenteredText(SMALL_FONT_ID, h / 2, fileBuf, true);

    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  void renderError() {
    int w = renderer.getScreenWidth();
    int h = renderer.getScreenHeight();
    const auto& m = UITheme::getInstance().getMetrics();

    renderer.clearScreen();
    GUI.drawHeader(renderer, Rect{0, m.topPadding, w, m.headerHeight}, tr(STR_APPS_STORE), tr(STR_APPS_ERROR));

    renderer.drawCenteredText(UI_12_FONT_ID, h / 2 - 40, errorTitle_.empty() ? tr(STR_APPS_ERROR) : errorTitle_.c_str(), true,
                              EpdFontFamily::BOLD);
    auto lines = renderer.wrappedText(SMALL_FONT_ID, errorMessage_.c_str(), w - 64, 4);
    int dy = h / 2;
    for (const auto& l : lines) {
      renderer.drawCenteredText(SMALL_FONT_ID, dy, l.c_str(), true);
      dy += renderer.getLineHeight(SMALL_FONT_ID) + 2;
    }

    drawActionButton(tr(STR_APPS_RETRY));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_APPS_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
};
