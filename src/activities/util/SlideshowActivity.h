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
  // Whatever orientation the app was already in when Slideshow started -
  // renderCurrentImage() may temporarily switch to whichever of Portrait/
  // Landscape best matches each individual image's own aspect ratio (see its
  // own comment for why), restored here on exit so leaving Slideshow doesn't
  // leave Home/whatever's next stuck in a rotated orientation.
  GfxRenderer::Orientation entryOrientation = GfxRenderer::Portrait;
};
