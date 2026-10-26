#pragma once

#if CROSSINK_BLUETOOTH

#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "BluetoothInput.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Settings > System > Bluetooth: the on/off switch, searching for and pairing
// a controller or page turner (BLE HID), paired devices, and learning which of
// the controller's buttons is which (Game Boy Up..Select).
class BluetoothActivity final : public Activity {
 public:
  explicit BluetoothActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  using UiApp = freeink::ui::FreeInkApp<40, 4>;

  enum class RowKind : uint8_t { Switch, Search, Buttons, Connected, Paired, Found, ForgetAll };
  struct Row {
    RowKind kind;
    uint8_t index;  // paired / found device index
  };

  ButtonNavigator buttonNavigator;
  freeink::ui::GfxRendererTarget uiTarget;  // must precede `app`: the app holds a reference to it
  UiApp app;
  std::atomic<bool> uiReady{false};
  int selectedIndex = 0;
  int visibleRows = 1;
  int topIndex = 0;

  std::vector<Row> rows;
  // Labels the list points into; rebuilt with the rows.
  std::vector<std::string> labels;
  std::vector<std::string> subtitles;
  char status[112] = {};
  uint8_t lastDeviceCount = 0;
  bool lastConnected = false;
  bool lastScanning = false;

  // Learning the controller's buttons, one control at a time.
  bool learning = false;
  uint8_t learnStep = 0;
  uint32_t learnCodes[bt::ControlCount] = {};

  static void listScreen(UiApp::ScreenType& screen, void* user);
  static void onRowEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildListScreen(UiApp::ScreenType& screen);
  void rebuildRows();
  void activate(const Row& row);
  void startLearning();
  void finishLearning(bool save);
  void loopLearning();
  void renderLearning();
};

#endif
