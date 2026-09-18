#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/OptionPopup.h"

class SlideshowActivity final : public Activity {
 public:
  SlideshowActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return screen == Screen::Playing; }

 private:
  enum class Screen { Menu, Playing, Empty };

  // Screen::Menu
  void refreshMenuPopup();
  void onMenuOptionSelected(int index);
  void openChooseFolder();
  void openIntervalPicker();
  void toggleScaleMode();

  // Screen::Playing / Screen::Empty
  void startPlayback();
  void loadImageList();
  void renderCurrentImage();
  void advanceImage(int delta);
  void drawEmptyMessage();

  Screen screen = Screen::Menu;
  OptionPopup optionPopup;

  std::vector<std::string> images;  // filenames only, sorted, within slideshowFolderPath
  int currentIndex = -1;
  unsigned long lastAdvanceMs = 0;
  // Same cadence idiom as ReaderUtils::displayWithRefreshCycle()'s
  // pagesUntilFullRefresh - counts down to 1 (SETTINGS.getRefreshFrequency()
  // images apart), then forces one FULL_REFRESH pass before the next image is
  // drawn, to settle the panel and clear accumulated ghosting/haze. See
  // renderCurrentImage()'s own comment for why this is needed at all on top
  // of the per-image grayscale composite.
  int imagesUntilFullRefresh = 1;
};
