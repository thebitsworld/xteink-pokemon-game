#include "SlideshowActivity.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <ImageToneProfile.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "Epub/converters/PngToFramebufferConverter.h"
#include "activities/util/PhotoRender.h"
#include "activities/home/FileBrowserActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "fontIds.h"

namespace {

bool isViewableImageFile(const std::string& filename) {
  return FsHelpers::hasBmpExtension(filename) || FsHelpers::hasPngExtension(filename);
}

bool isMacOSSidecarFile(const std::string& filename) { return filename.rfind("._", 0) == 0; }

void drawSlideshowMessage(GfxRenderer& renderer, const MappedInputManager& mappedInput, const char* message) {
  renderer.clearScreen();
  renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2, message);
  const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

// Seconds between auto-advances, converted to milliseconds for millis() comparisons.
unsigned long intervalMs() { return static_cast<unsigned long>(SETTINGS.slideshowIntervalSeconds) * 1000UL; }

// Left running unattended (no real button/touch/swipe input, only timer-driven
// auto-advances) for this long, the slideshow exits back to Home on its own.
constexpr unsigned long AUTO_EXIT_IDLE_MS = 60UL * 60UL * 1000UL;

}  // namespace

SlideshowActivity::SlideshowActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     const bool fromApplications)
    : Activity("Slideshow", renderer, mappedInput), fromApplications(fromApplications) {}

void SlideshowActivity::leave() {
  if (fromApplications) {
    finish();
  } else {
    activityManager.goHome();
  }
}

void SlideshowActivity::onEnter() {
  Activity::onEnter();
  if (screen == Screen::Menu) {
    renderer.clearScreen();
    refreshMenuPopup();
  } else {
    startPlayback();
  }
  requestUpdate();
}

void SlideshowActivity::onExit() {
  Activity::onExit();
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SlideshowActivity::refreshMenuPopup() {
  const std::string folderLabel =
      std::string(tr(STR_SLIDESHOW_CHOOSE_FOLDER)) + ": " +
      (APP_STATE.slideshowFolderPath.empty() ? tr(STR_SLIDESHOW_FOLDER_NOT_SET) : APP_STATE.slideshowFolderPath);

  char intervalText[32];
  snprintf(intervalText, sizeof(intervalText), tr(STR_SECONDS_VALUE_FORMAT), SETTINGS.slideshowIntervalSeconds);
  const std::string intervalLabel = std::string(tr(STR_SLIDESHOW_INTERVAL)) + ": " + intervalText;

  const bool isCrop = SETTINGS.slideshowScaleMode == CrossPointSettings::SLIDESHOW_CROP;
  const std::string displayModeLabel =
      std::string(tr(STR_SLIDESHOW_DISPLAY_MODE)) + ": " + (isCrop ? tr(STR_CROP) : tr(STR_FIT));

  const std::string randomOrderLabel =
      std::string(tr(STR_SLIDESHOW_RANDOM_ORDER)) + ": " + (SETTINGS.slideshowRandomOrder ? tr(STR_ON) : tr(STR_OFF));

  const std::string startLabel = tr(STR_SLIDESHOW_START);

  std::vector<std::string> options{folderLabel, intervalLabel, displayModeLabel, randomOrderLabel, startLabel};
  optionPopup.show(tr(STR_SLIDESHOW), options, 0, [this](const int index) { onMenuOptionSelected(index); });
  optionPopup.setDisabledOptions({false, false, false, false, APP_STATE.slideshowFolderPath.empty()});
  optionPopup.setCancelCallback([this] { leave(); });
}

void SlideshowActivity::onMenuOptionSelected(const int index) {
  switch (index) {
    case 0:
      openChooseFolder();
      return;
    case 1:
      openIntervalPicker();
      return;
    case 2:
      toggleScaleMode();
      return;
    case 3:
      toggleRandomOrder();
      return;
    case 4:
      if (!APP_STATE.slideshowFolderPath.empty()) {
        screen = Screen::Playing;
        startPlayback();
      }
      return;
    default:
      return;
  }
}

void SlideshowActivity::openChooseFolder() {
  const std::string initialPath = APP_STATE.slideshowFolderPath.empty() ? "/" : APP_STATE.slideshowFolderPath;
  auto picker = std::make_unique<FileBrowserActivity>(renderer, mappedInput, initialPath,
                                                      FileBrowserActivity::Mode::PickDirectory);
  startActivityForResult(std::move(picker), [this](const ActivityResult& result) {
    const auto* path = std::get_if<FilePathResult>(&result.data);
    if (!result.isCancelled && path) {
      APP_STATE.slideshowFolderPath = path->path == "/" ? std::string() : path->path;
      APP_STATE.saveToFile();
    }
    refreshMenuPopup();
    requestUpdate();
  });
}

void SlideshowActivity::openIntervalPicker() {
  startActivityForResult(
      std::make_unique<IntervalSelectionActivity>(
          renderer, mappedInput, "SlideshowMenuIntervalInterval", StrId::STR_SLIDESHOW_INTERVAL,
          SETTINGS.slideshowIntervalSeconds, CrossPointSettings::MIN_SLIDESHOW_INTERVAL_SECONDS,
          CrossPointSettings::MAX_SLIDESHOW_INTERVAL_SECONDS, 10, 30, StrId::STR_SECONDS_VALUE_FORMAT,
          /*readerActivity=*/false, /*allowPowerAsConfirm=*/false, /*ignoreInitialConfirmRelease=*/true,
          /*showPercentValue=*/false, StrId::STR_NONE_OPT, /*overrideDisabledReaderTouchscreen=*/false,
          /*showTouchHeaderBackButton=*/true, /*valueFormatter=*/nullptr, /*tapStep=*/0,
          /*useReaderSlider=*/true),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          SETTINGS.slideshowIntervalSeconds = static_cast<uint16_t>(std::get<IntervalResult>(result.data).value);
          SETTINGS.saveToFile();
        }
        refreshMenuPopup();
        requestUpdate();
      });
}

void SlideshowActivity::toggleScaleMode() {
  SETTINGS.slideshowScaleMode = SETTINGS.slideshowScaleMode == CrossPointSettings::SLIDESHOW_CROP
                                    ? CrossPointSettings::SLIDESHOW_FIT
                                    : CrossPointSettings::SLIDESHOW_CROP;
  SETTINGS.saveToFile();
  refreshMenuPopup();
  requestUpdate();
}

void SlideshowActivity::toggleRandomOrder() {
  SETTINGS.slideshowRandomOrder = SETTINGS.slideshowRandomOrder ? 0 : 1;
  SETTINGS.saveToFile();
  refreshMenuPopup();
  requestUpdate();
}

void SlideshowActivity::render(RenderLock&&) {
  if (screen != Screen::Menu) return;
  renderer.clearScreen();
  optionPopup.processRender(renderer, mappedInput);
}

void SlideshowActivity::loadImageList() {
  images.clear();
  currentIndex = -1;

  const std::string& dirPath = APP_STATE.slideshowFolderPath;
  if (dirPath.empty()) return;

  auto dir = Storage.open(dirPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }

  char name[500];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (!file.isDirectory()) {
      file.getName(name, sizeof(name));
      if (name[0] != '.' && !isMacOSSidecarFile(name)) {
        std::string fname(name);
        if (isViewableImageFile(fname)) {
          images.push_back(fname);
        }
      }
    }
    file.close();
  }
  dir.close();

  FsHelpers::sortFileList(images);
  if (SETTINGS.slideshowRandomOrder && images.size() > 1) {
    // Fisher-Yates: shuffle once per playback session rather than picking a
    // fresh random image on every advance, so Prev/Next still move
    // predictably back and forth through a stable (if shuffled) order.
    for (size_t i = images.size() - 1; i > 0; --i) {
      const size_t j = static_cast<size_t>(random(static_cast<long>(i + 1)));
      std::swap(images[i], images[j]);
    }
  }
  if (!images.empty()) currentIndex = 0;
}

void SlideshowActivity::startPlayback() {
  loadImageList();
  if (images.empty()) {
    screen = Screen::Empty;
    drawEmptyMessage();
    return;
  }
  screen = Screen::Playing;
  lastAdvanceMs = millis();
  lastInteractionMs = millis();
  renderCurrentImage();
}

void SlideshowActivity::drawEmptyMessage() { drawSlideshowMessage(renderer, mappedInput, tr(STR_SLIDESHOW_EMPTY_FOLDER)); }

void SlideshowActivity::renderCurrentImage() {
  if (currentIndex < 0 || currentIndex >= static_cast<int>(images.size())) return;

  std::string dirPath = APP_STATE.slideshowFolderPath;
  if (dirPath.back() != '/') dirPath += "/";
  const std::string filePath = dirPath + images[currentIndex];
  const bool cropMode = SETTINGS.slideshowScaleMode == CrossPointSettings::SLIDESHOW_CROP;

  // Drawing (and the power lock it needs on the X3) lives in photo::render(), shared with
  // Desk mode's photo frame.
  switch (photo::render(renderer, filePath, cropMode)) {
    case photo::Result::Ok:
      return;
    case photo::Result::ImageError:
      drawSlideshowMessage(renderer, mappedInput, tr(STR_SLIDESHOW_IMAGE_ERROR));
      return;
    case photo::Result::RenderFailed:
      drawSlideshowMessage(renderer, mappedInput, tr(STR_FAILED_LOWER));
      return;
  }
}

void SlideshowActivity::advanceImage(const int delta) {
  if (images.empty()) return;
  const int count = static_cast<int>(images.size());
  currentIndex = ((currentIndex + delta) % count + count) % count;
  lastAdvanceMs = millis();
  renderCurrentImage();
}

void SlideshowActivity::loop() {
  Activity::loop();

  if (screen == Screen::Menu) {
    optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
    return;
  }

  // Screen::Playing and Screen::Empty: Back leaves straight away (to Home, or
  // back to Applications).
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    leave();
    return;
  }

  if (screen != Screen::Playing) return;

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Left ||
      mappedInput.wasReleased(MappedInputManager::Button::Right) ||
      mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    lastInteractionMs = millis();
    advanceImage(1);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Right || mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    lastInteractionMs = millis();
    advanceImage(-1);
    return;
  }

  // Left running unattended - not the same as the normal auto-sleep timeout,
  // which this activity deliberately blocks while Playing (see
  // preventAutoSleep()) so the display keeps auto-advancing on its own.
  if (millis() - lastInteractionMs >= AUTO_EXIT_IDLE_MS) {
    leave();
    return;
  }

  if (millis() - lastAdvanceMs >= intervalMs()) {
    advanceImage(1);
  }
}
