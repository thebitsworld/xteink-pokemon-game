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
};
