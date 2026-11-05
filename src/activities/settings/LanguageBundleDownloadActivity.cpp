#include "LanguageBundleDownloadActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstdio>

#include "LanguageBundle.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"

namespace {

constexpr int DOWNLOAD_MAX_ATTEMPTS = 3;
constexpr uint32_t DOWNLOAD_RETRY_DELAY_MS = 500;

// The X3 has no serial console: what happened goes on the SD card too.
void noteResult(const char* step, const int code) {
  LOG_INF("LANGBUNDLE", "%s: %d (free=%u maxAlloc=%u)", step, code, static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
  HalFile log = Storage.open("/.crosspoint/language-bundle-log.txt", O_WRONLY | O_CREAT | O_APPEND);
  if (!log) return;
  char line[96];
  const int length = snprintf(line, sizeof(line), "%s: %d free=%u maxAlloc=%u\n", step, code,
                              static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
  if (length > 0) log.write(line, static_cast<size_t>(length));
  log.close();
}

bool validateBundle(const std::string& path, void* expectedSha256) {
  return language_bundle::fileMatchesSha256(path, *static_cast<const std::string*>(expectedSha256));
}

}  // namespace

void LanguageBundleDownloadActivity::onEnter() {
  Activity::onEnter();
  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void LanguageBundleDownloadActivity::onExit() {
  Activity::onExit();
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    WiFi.mode(WIFI_OFF);
  }
  // The boot unpacks the download; it also gives back the heap Wi-Fi split.
  if (restartRequested_) silentRestart();
}

void LanguageBundleDownloadActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    finish();
    return;
  }
  download();
}

bool LanguageBundleDownloadActivity::pollCancelInput() {
  if (cancelRequested_) return true;
  mappedInput.update();
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      (mappedInput.hasTouchHardware() && TouchHeaderBackButton::wasTapped(mappedInput, renderer))) {
    cancelRequested_ = true;
  }
  return cancelRequested_;
}

void LanguageBundleDownloadActivity::download() {
  {
    RenderLock lock(*this);
    state_ = DOWNLOADING;
    downloaded_ = 0;
    total_ = 0;
    cancelRequested_ = false;
  }
  requestUpdateAndWait();

  // The checksum first: the bundle is only kept if it matches.
  std::string expected;
  noteResult("start", 0);
  const bool fetched = HttpDownloader::fetchUrl(language_bundle::SHA256_URL, expected);
  noteResult("checksum", fetched ? static_cast<int>(expected.size()) : -1);
  if (fetched && expected.size() >= 64) {
    expected.resize(64);
  } else {
    LOG_ERR("LANGBUNDLE", "Could not fetch the bundle checksum");
    expected.clear();
  }
  if (expected.empty()) {
    {
      RenderLock lock(*this);
      state_ = ERROR;
    }
    requestUpdate();
    return;
  }

  HttpDownloader::DownloadOptions options;
  options.stageAsPart = true;  // a failed download never reaches the path the boot looks at
  options.validateWithContext = &validateBundle;
  options.validationContext = &expected;
  options.checkFreeSpace = true;
  // As for firmware updates: wolfSSL has no CA bundle, but the checksum fetched above over the verified client
  // pins these bytes. It takes far less RAM than esp_http_client, which has also stalled on C3 file downloads.
  options.transport = HttpDownloader::Transport::WOLFSSL;
  options.shouldCancel = [this]() { return pollCancelInput(); };
  HttpDownloader::DownloadError result = HttpDownloader::HTTP_ERROR;
  for (int attempt = 1; attempt <= DOWNLOAD_MAX_ATTEMPTS && result != HttpDownloader::OK; ++attempt) {
    if (attempt > 1) delay(DOWNLOAD_RETRY_DELAY_MS);
    result = HttpDownloader::downloadToFile(
        language_bundle::URL, language_bundle::DOWNLOAD_PATH,
        [this](const size_t downloaded, const size_t total) {
          downloaded_ = downloaded;
          total_ = total;
          requestUpdate(true);
        },
        &cancelRequested_, "", "", options);
    if (result == HttpDownloader::ABORTED) break;
    noteResult("download", static_cast<int>(result));
  }

  if (result == HttpDownloader::ABORTED) {
    mappedInput.suppressNextBackRelease();
    finish();
    return;
  }
  {
    RenderLock lock(*this);
    state_ = result == HttpDownloader::OK ? COMPLETE : ERROR;
  }
  requestUpdate();
}

void LanguageBundleDownloadActivity::loop() {
  if (state_ == COMPLETE) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
        mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
      restartRequested_ = true;
      finish();
    }
  } else if (state_ == ERROR) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        (mappedInput.hasTouchHardware() && TouchHeaderBackButton::wasTapped(mappedInput, renderer))) {
      finishAfterBackPress();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      download();
    }
  }
}

void LanguageBundleDownloadActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();

  const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInput);
  GUI.drawHeader(renderer, header, tr(STR_LANGUAGE_PACK));

  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int centerY = (pageHeight - lineHeight) / 2;
  const Rect textArea{metrics.contentSidePadding, 0, pageWidth - metrics.contentSidePadding * 2, pageHeight};

  if (state_ == DOWNLOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight, tr(STR_LANGUAGE_PACK_DOWNLOADING));
    const int percent = total_ > 0 ? static_cast<int>(downloaded_ * 100 / total_) : 0;
    GUI.drawProgressBar(renderer,
                        Rect{metrics.contentSidePadding, centerY + metrics.verticalSpacing,
                             pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
                        percent, 100);
    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == COMPLETE) {
    UITheme::drawCenteredWrappedText(renderer, textArea, UI_10_FONT_ID, centerY - lineHeight,
                                     tr(STR_LANGUAGE_PACK_READY), 3, true, EpdFontFamily::BOLD, 2);
    const auto labels = mappedInput.mapLabels("", tr(STR_OK), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == ERROR) {
    UITheme::drawCenteredWrappedText(renderer, textArea, UI_10_FONT_ID, centerY - lineHeight,
                                     tr(STR_LANGUAGE_PACK_FAILED), 3, true, EpdFontFamily::BOLD, 2);
    const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer(screenTransitionRefresh_.modeFor(static_cast<uint8_t>(state_)));
}
