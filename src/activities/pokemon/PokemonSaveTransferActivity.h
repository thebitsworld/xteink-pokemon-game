#pragma once

#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>
#include <HalStorage.h>
#include <NearbyTransfer.h>
#include <PokemonSaveBundleCodec.h>

#include <array>
#include <atomic>
#include <string>

#include "activities/Activity.h"
#include "activities/ScreenTransitionRefresh.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "pokemon/PokemonSaveTransfer.h"
#include "util/ButtonNavigator.h"

// Moves or copies the whole Pokemon save to another nearby device over
// ESP-NOW (no Wi-Fi network). Same packet protocol and reliable chunk/ack
// session as NearbyBookTransferActivity (freeink::nearby), but every
// Discover/Advertise/Offer payload carries SAVE_BUNDLE_TAG so it never pairs
// with a book transfer or stats sync screen. The SD-card side - bundle
// building, verification and the crash-safe install journal - lives in
// pokemon/PokemonSaveTransfer.h.
class PokemonSaveTransferActivity final : public Activity {
 public:
  enum class Mode : uint8_t { Send, Receive };

  PokemonSaveTransferActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Mode mode);
  ~PokemonSaveTransferActivity() override;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
  bool skipLoopDelay() override;

 private:
  enum class State : uint8_t {
    ChooseSendMode,
    Discovering,
    DeviceList,
    WaitingForApproval,
    Sending,
    Listening,
    OfferPrompt,
    Receiving,
    Installing,
    Success,
    Error,
  };

  struct Peer {
    std::array<uint8_t, freeink::nearby::MAC_BYTES> mac{};
    std::array<char, 33> name{};
  };

  static constexpr size_t MAX_PEERS = 4;
  static constexpr uint8_t ESPNOW_CHANNEL = 1;
  static constexpr uint32_t DISCOVERY_INTERVAL_MS = 900;
  static constexpr uint32_t RETRY_INTERVAL_MS = 450;
  static constexpr uint8_t MAX_RETRIES = 12;
  static constexpr uint8_t MAX_APPROVAL_RETRIES = 60;
  static constexpr uint32_t RECEIVE_TIMEOUT_MS = 15000;
  static constexpr uint32_t UI_REFRESH_MS = 1800;

  Mode mode_;
  State state_ = State::Error;
  ScreenTransitionRefresh screenTransitionRefresh_;
  bool radioUsed_ = false;
  bool moveSave_ = false;

  freeink::nearby::EspNowTransport transport_;
  freeink::nearby::EspNowTransport::Event eventBuffer_;
  freeink::nearby::ReliableTransferSession session_;
  std::array<Peer, MAX_PEERS> peers_{};
  uint8_t peerCount_ = 0;
  int selectedIndex_ = 0;
  int selectedPeer_ = 0;
  std::array<uint8_t, freeink::nearby::MAC_BYTES> peerMac_{};

  pokemon::save_transfer::BundleSource source_;
  HalFile receiveFile_;
  std::array<uint8_t, freeink::nearby::V2_CHUNK_BYTES> chunkBuffer_{};
  std::array<uint8_t, freeink::nearby::MAX_PACKET_BYTES> packetBuffer_{};
  size_t pendingChunkLength_ = 0;
  pokemon::SaveBundleOffer offer_{};
  uint16_t negotiatedChunkBytes_ = freeink::nearby::COMPAT_CHUNK_BYTES;
  uint32_t sessionId_ = 0;
  uint32_t lastActionMs_ = 0;
  uint32_t lastUiMs_ = 0;
  uint8_t retryCount_ = 0;
  bool receiverHasSave_ = false;
  bool installPending_ = false;
  std::string errorMessage_;

  ButtonNavigator buttonNavigator_;

  using UiApp = freeink::ui::FreeInkApp<8, 4>;
  static constexpr freeink::ui::ActionId ACTION_ROW = 1;
  freeink::ui::GfxRendererTarget uiTarget_;  // Must precede app_: app_ holds a reference to it.
  UiApp app_;
  std::atomic<bool> uiReady_{false};

  bool startRadio();
  void startListening();
  void startDiscovery();
  void selectPeer();
  void processPackets();
  void handleSenderPacket(const freeink::nearby::EspNowTransport::Event& event,
                          const freeink::nearby::PacketView& packet);
  void handleReceiverPacket(const freeink::nearby::EspNowTransport::Event& event,
                            const freeink::nearby::PacketView& packet);
  bool sendPacket(freeink::nearby::PacketType type, const uint8_t* destination, uint32_t sequence = 0,
                  const void* payload = nullptr, uint16_t payloadLength = 0);
  bool sendDiscovery();
  bool sendAdvertisement(const uint8_t* destination);
  bool sendOffer();
  void acceptOffer();
  void rejectOffer();
  bool sendNextChunk();
  bool resendPending();
  bool sendAck();
  bool sendComplete();
  void sendResult(bool ok);
  bool finishReceivedBundle(uint64_t expectedBytes, uint32_t expectedCrc);
  void installReceivedBundle();
  void finishSend();
  void cancelTransfer();
  void setState(State state);
  void setError(const char* message);
  void exitAfterRadio();
  void updateTimers();
  void maybeRefreshProgress();
  bool isMenuState() const;
  int menuItemCount() const;
  void activateSelected();
  void updateNavigation();
  bool fillOffer();
  static void menuScreen(UiApp::ScreenType& screen, void* user);
  static void onRowEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildMenuScreen(UiApp::ScreenType& screen);
};
