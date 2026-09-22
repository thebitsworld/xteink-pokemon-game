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
  // Auto-advances are timer-driven, not user input - there's nothing that
  // needs full CPU speed between them, so let the main loop downclock as
  // usual once idle. Real button/touch input still restores full speed via
  // the normal userInputReceived path.
  bool needsFullPowerWhilePreventingSleep() override { return false; }

 private:
  enum class Screen { Menu, Playing, Empty };

  // Screen::Menu
  void refreshMenuPopup();
  void onMenuOptionSelected(int index);
  void openChooseFolder();
  void openIntervalPicker();
  void toggleScaleMode();
  void toggleRandomOrder();

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
  // Real button/touch/swipe navigation only - NOT reset by timer-driven
  // auto-advances, so a slideshow left running unattended still auto-exits.
  unsigned long lastInteractionMs = 0;
};
