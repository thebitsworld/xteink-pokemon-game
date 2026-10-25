#include "BluetoothActivity.h"

#if CROSSINK_BLUETOOTH

#include <BleKeyboardHost.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_ROW = 1;
constexpr uint32_t SCAN_MS = 10000;

StrId controlName(const uint8_t control) {
  switch (control) {
    case bt::Up: return StrId::STR_BT_CONTROL_UP;
    case bt::Down: return StrId::STR_BT_CONTROL_DOWN;
    case bt::Left: return StrId::STR_BT_CONTROL_LEFT;
    case bt::Right: return StrId::STR_BT_CONTROL_RIGHT;
    case bt::A: return StrId::STR_BT_CONTROL_A;
    case bt::B: return StrId::STR_BT_CONTROL_B;
    case bt::Start: return StrId::STR_BT_CONTROL_START;
    default: return StrId::STR_BT_CONTROL_SELECT;
  }
}

bool isPaired(const char* addr) {
  for (uint8_t i = 0; i < BleHid.pairedCount(); ++i) {
    if (std::strcmp(BleHid.paired(i).addr, addr) == 0) return true;
  }
  return false;
}
}  // namespace

BluetoothActivity::BluetoothActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Bluetooth", renderer, mappedInput),
      uiTarget(makeUiTarget(renderer)),
      app(uiTarget, uiTarget.deviceContext()) {}

void BluetoothActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  topIndex = 0;
  visibleRows = 1;
  uiReady = false;
  status[0] = '\0';
  learning = false;
  rebuildRows();
  applySharedUiTheme(app, uiTarget);
  app.on(ACTION_ROW, &BluetoothActivity::onRowEvent, this);
  app.setScreen(&BluetoothActivity::listScreen, this);
  requestUpdate();
}

void BluetoothActivity::onExit() {
  if (learning) bt::setLearning(false);
  if (BleHid.isScanning()) BleHid.stopScan();
  Activity::onExit();
}

void BluetoothActivity::rebuildRows() {
  rows.clear();
  labels.clear();
  subtitles.clear();
  const bool on = bt::running();
  auto add = [this](const RowKind kind, const uint8_t index, std::string label, std::string subtitle) {
    rows.push_back({kind, index});
    labels.push_back(std::move(label));
    subtitles.push_back(std::move(subtitle));
  };

  add(RowKind::Switch, 0, tr(STR_BLUETOOTH), on ? tr(STR_STATE_ON) : tr(STR_STATE_OFF));
  if (on) {
    std::string searchSubtitle = status[0] ? status : (BleHid.isScanning() ? tr(STR_BT_SEARCHING) : "");
    add(RowKind::Search, 0, tr(STR_BT_SEARCH), searchSubtitle);
    add(RowKind::Buttons, 0, tr(STR_BT_CONTROLLER_BUTTONS),
        bt::hasLearnedCodes() ? tr(STR_BT_BUTTONS_LEARNED) : tr(STR_BT_BUTTONS_DEFAULT));
    if (BleHid.isConnected()) {
      add(RowKind::Connected, 0, BleHid.connectedName(), tr(STR_BT_CONNECTED_TAP_TO_DISCONNECT));
    }
    for (uint8_t i = 0; i < BleHid.pairedCount(); ++i) {
      const auto& device = BleHid.paired(i);
      if (BleHid.isConnected() && std::strcmp(device.addr, BleHid.connectedAddr()) == 0) continue;
      add(RowKind::Paired, i, device.name[0] ? device.name : device.addr,
          BleHid.isConnecting() ? tr(STR_BT_CONNECTING) : tr(STR_BT_PAIRED_TAP_TO_CONNECT));
    }
    for (uint8_t i = 0; i < BleHid.deviceCount(); ++i) {
      const auto& device = BleHid.device(i);
      if (!device.hid || !device.connectable || isPaired(device.addr)) continue;
      add(RowKind::Found, i, device.name, tr(STR_BT_TAP_TO_PAIR));
    }
    if (BleHid.pairedCount() > 0) add(RowKind::ForgetAll, 0, tr(STR_BT_FORGET_ALL), "");
  }
  if (selectedIndex >= static_cast<int>(rows.size())) selectedIndex = static_cast<int>(rows.size()) - 1;
  if (selectedIndex < 0) selectedIndex = 0;
}

void BluetoothActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<BluetoothActivity*>(user);
  if (event.value < 0 || event.value >= static_cast<int16_t>(self->rows.size())) return;
  self->selectedIndex = event.value;
  self->app.clearTapFlash();
  self->activate(self->rows[static_cast<size_t>(event.value)]);
  self->requestUpdate();
}

void BluetoothActivity::activate(const Row& row) {
  status[0] = '\0';
  switch (row.kind) {
    case RowKind::Switch:
      if (!bt::setEnabled(!bt::running())) {
        std::snprintf(status, sizeof(status), "%s", tr(STR_BT_START_FAILED));
      } else if (bt::running()) {
        BleHid.startScan(SCAN_MS);
      }
      break;
    case RowKind::Search:
      BleHid.startScan(SCAN_MS);
      break;
    case RowKind::Buttons:
      startLearning();
      break;
    case RowKind::Connected:
      BleHid.disconnect();
      break;
    case RowKind::Paired:
      if (row.index < BleHid.pairedCount()) BleHid.connect(BleHid.paired(row.index).addr);
      break;
    case RowKind::Found:
      if (row.index < BleHid.deviceCount()) {
        if (BleHid.isScanning()) BleHid.stopScan();
        if (BleHid.connect(BleHid.device(row.index).addr)) {
          std::snprintf(status, sizeof(status), "%s", tr(STR_BT_CONNECTING));
        }
      }
      break;
    case RowKind::ForgetAll:
      while (BleHid.pairedCount() > 0) BleHid.forget(BleHid.paired(0).addr);
      break;
  }
  rebuildRows();
}

void BluetoothActivity::loop() {
  if (learning) {
    loopLearning();
    return;
  }

  // What the radio reports changes the rows: found devices, links, PINs.
  if (bt::running()) {
    char failure[48] = {};
    uint32_t passkey = 0;
    bool changed = false;
    if (BleHid.takeConnectFailure(failure, sizeof(failure))) {
      std::snprintf(status, sizeof(status), "%s: %.40s", tr(STR_BT_CONNECT_FAILED), failure);
      changed = true;
    }
    if (BleHid.takePairingPasskey(passkey)) {
      std::snprintf(status, sizeof(status), "%s %06lu", tr(STR_BT_PIN), static_cast<unsigned long>(passkey));
      changed = true;
    }
    if (BleHid.isConnected() != lastConnected) {
      lastConnected = BleHid.isConnected();
      if (lastConnected) status[0] = '\0';
      changed = true;
    }
    if (BleHid.deviceCount() != lastDeviceCount || BleHid.isScanning() != lastScanning) {
      lastDeviceCount = BleHid.deviceCount();
      lastScanning = BleHid.isScanning();
      changed = true;
    }
    if (changed) {
      rebuildRows();
      requestUpdate();
    }
  }

  if (TouchHeaderBackButton::wasTapped(mappedInput, renderer) ||
      mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectedIndex < static_cast<int>(rows.size())) activate(rows[static_cast<size_t>(selectedIndex)]);
    requestUpdate();
    return;
  }
  if (uiReady) {
    const fui::InputSnapshot snap = touchSnapshotFrom(mappedInput);
    if (snap.touchPressed || snap.touchReleased) {
      const auto event = app.route(snap);
      if (app.invalidated()) requestUpdate();
      if (event) return;
    }
  }
  const int itemCount = static_cast<int>(rows.size());
  if (itemCount > 0) {
    const auto moveSelection = [this, itemCount](const int index) {
      selectedIndex = index;
      topIndex = followListSelection(selectedIndex, topIndex, visibleRows, itemCount);
      requestUpdate();
    };
    buttonNavigator.onNext(
        [this, itemCount, &moveSelection] { moveSelection(ButtonNavigator::nextIndex(selectedIndex, itemCount)); });
    buttonNavigator.onPrevious(
        [this, itemCount, &moveSelection] { moveSelection(ButtonNavigator::previousIndex(selectedIndex, itemCount)); });
  }
}

void BluetoothActivity::startLearning() {
  learning = true;
  learnStep = 0;
  std::memset(learnCodes, 0, sizeof(learnCodes));
  bt::setLearning(true);
  requestUpdate();
}

void BluetoothActivity::finishLearning(const bool save) {
  bt::setLearning(false);
  learning = false;
  if (save) {
    bt::setLearnedCodes(learnCodes);
    std::snprintf(status, sizeof(status), "%s", tr(STR_BT_BUTTONS_SAVED));
  }
  rebuildRows();
  requestUpdate();
}

void BluetoothActivity::loopLearning() {
  // The reader's own buttons steer this screen: the controller's presses are
  // being learned, not injected. Back cancels, Confirm skips a control (it then
  // keeps its default key), Left clears the learned buttons.
  if (TouchHeaderBackButton::wasTapped(mappedInput, renderer) ||
      mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finishLearning(false);
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    bt::clearLearnedCodes();
    bt::setLearning(false);
    learning = false;
    std::snprintf(status, sizeof(status), "%s", tr(STR_BT_BUTTONS_DEFAULT));
    rebuildRows();
    requestUpdate();
    return;
  }
  uint32_t code = 0;
  const bool skipped = mappedInput.wasPressed(MappedInputManager::Button::Confirm);
  if (!skipped && !bt::takeLearnedPress(code)) return;
  if (!skipped) {
    // One button cannot stand for two controls.
    for (uint8_t i = 0; i < learnStep; ++i) {
      if (learnCodes[i] == code) return;
    }
    learnCodes[learnStep] = code;
  }
  ++learnStep;
  if (learnStep >= bt::ControlCount) {
    finishLearning(true);
    return;
  }
  requestUpdate();
}

void BluetoothActivity::listScreen(UiApp::ScreenType& screen, void* user) {
  static_cast<BluetoothActivity*>(user)->buildListScreen(screen);
}

void BluetoothActivity::buildListScreen(UiApp::ScreenType& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(metrics.topPadding + TouchHeaderBackButton::height(metrics, mappedInput)), 0,
                  static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  std::vector<fui::ListItem> items;
  items.reserve(rows.size());
  for (size_t i = 0; i < rows.size(); ++i) {
    fui::ListItem item;
    item.label = labels[i].c_str();
    if (!subtitles[i].empty()) item.subtitle = subtitles[i].c_str();
    item.actionValue = static_cast<int16_t>(i);
    items.push_back(item);
  }
  fui::ListProps props;
  props.items = items.data();
  props.count = static_cast<uint16_t>(items.size());
  props.selectedIndex = static_cast<int16_t>(selectedIndex);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  const auto shown = configureUiList(props, screen.theme(), screen.body(), UiListRowType::WithSubtitle);
  visibleRows = shown > 0 ? shown : 1;
  topIndex = scrollListBy(topIndex, 0, visibleRows, static_cast<int>(items.size()));
  props.topIndex = static_cast<uint16_t>(topIndex);
  screen.list(props);
}

void BluetoothActivity::renderLearning() {
  const int midY = renderer.getScreenHeight() / 2;
  char line[96];
  std::snprintf(line, sizeof(line), "%s (%u/%u)", tr(STR_BT_PRESS_ON_CONTROLLER), static_cast<unsigned>(learnStep + 1),
                static_cast<unsigned>(bt::ControlCount));
  renderer.drawCenteredText(UI_10_FONT_ID, midY - 40, line);
  renderer.drawCenteredText(UI_12_FONT_ID, midY, I18N.get(controlName(learnStep)), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, midY + 40, tr(STR_BT_LEARN_HINT));
  const auto hints = mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_CANCEL)), tr(STR_BT_SKIP),
                                           tr(STR_BT_RESET_BUTTONS), "");
  GUI.drawButtonHints(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
}

void BluetoothActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInput);
  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::draw(renderer, uiTarget, header, tr(STR_BLUETOOTH), false);
  } else {
    GUI.drawHeader(renderer, header, tr(STR_BLUETOOTH));
  }

  if (learning) {
    uiReady = false;
    renderLearning();
    renderer.displayBuffer();
    return;
  }

  uiReady = false;
  app.render();
  uiReady = true;
  const auto hints =
      mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
  renderer.displayBuffer();
}

#endif
