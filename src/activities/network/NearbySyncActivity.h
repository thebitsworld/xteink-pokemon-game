#pragma once

#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>
#include <HalStorage.h>
#include <NearbySyncCodec.h>
#include <NearbyTransfer.h>

#include <array>
#include <atomic>
#include <memory>
#include <string>

#include "activities/Activity.h"
#include "activities/ScreenTransitionRefresh.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "network/NearbySyncStore.h"
#include "util/ButtonNavigator.h"

// "Sync with nearby reader": one ESP-NOW transfer (no Wi-Fi network) that
// moves everything worth moving from one reader to another in a single go -
// the Pokemon save (Pokemon builds), the reading stats (exchanged both ways:
// the receiver's own stats ride back on its final reply), and the position in
// the book that was last open on the sender. Same packet protocol and
// reliable chunk/ack session as NearbyBookTransferActivity (freeink::nearby);
// every Discover/Advertise/Offer payload carries nearby_sync::OFFER_TAG so it
// never pairs with the book transfer, stats sync or position sync screens.
//
// Scope::PokemonOnly is the same flow limited to the Pokemon save (the
// Pokemon menu's Send/Receive Save entries).
class NearbySyncActivity final : public Activity {
 public:
  enum class Role : uint8_t { Ask, Send, Receive };
  enum class Scope : uint8_t { Everything, PokemonOnly };

  NearbySyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Role role, Scope scope);
  ~NearbySyncActivity() override;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
  bool skipLoopDelay() override;

 private:
  enum class State : uint8_t {
    ChooseRole,
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
    std::array<char, nearby_sync::NAME_BYTES> name{};
  };

  // What the receiver did with each section, for the final screen.
  enum class Outcome : uint8_t { None, Applied, Failed, BookMissing };

  static constexpr size_t MAX_PEERS = 4;
  static constexpr uint8_t ESPNOW_CHANNEL = 1;
  static constexpr uint32_t DISCOVERY_INTERVAL_MS = 900;
  static constexpr uint32_t RETRY_INTERVAL_MS = 450;
  static constexpr uint8_t MAX_RETRIES = 12;
  static constexpr uint8_t MAX_APPROVAL_RETRIES = 60;
  static constexpr uint32_t RECEIVE_TIMEOUT_MS = 15000;
  static constexpr uint32_t UI_REFRESH_MS = 1800;

  Role role_;
  Scope scope_;
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
  std::array<uint8_t, nearby_sync::MAC_BYTES> localMac_{};

  // Sender: what goes out. The Pokemon section streams from SD.
  nearby_sync::ContainerSource source_;
  std::unique_ptr<nearby_sync::SectionStream> pokemonStream_;
  bool hasPokemon_ = false;
  bool hasStats_ = false;
  bool hasBook_ = false;
  nearby_sync::BookPositionRecord book_{};

  HalFile receiveFile_;
  std::array<uint8_t, freeink::nearby::V2_CHUNK_BYTES> chunkBuffer_{};
  std::array<uint8_t, freeink::nearby::MAX_PACKET_BYTES> packetBuffer_{};
  size_t pendingChunkLength_ = 0;
  nearby_sync::Offer offer_{};
  uint16_t negotiatedChunkBytes_ = freeink::nearby::COMPAT_CHUNK_BYTES;
  uint32_t sessionId_ = 0;
  uint32_t lastActionMs_ = 0;
  uint32_t lastUiMs_ = 0;
  uint8_t retryCount_ = 0;

  // Receiver.
  bool receiverHasPokemonSave_ = false;
  std::array<uint8_t, nearby_sync::RESULT_MAX_BYTES> resultBuffer_{};
  size_t resultLength_ = 0;
  Outcome pokemonOutcome_ = Outcome::None;
  Outcome statsOutcome_ = Outcome::None;
  Outcome bookOutcome_ = Outcome::None;
  bool installPending_ = false;
  std::string receivedBookPath_;
  std::string errorMessage_;

  ButtonNavigator buttonNavigator_;

  using UiApp = freeink::ui::FreeInkApp<8, 4>;
  static constexpr freeink::ui::ActionId ACTION_ROW = 1;
  freeink::ui::GfxRendererTarget uiTarget_;  // Must precede app_: app_ holds a reference to it.
  UiApp app_;
  std::atomic<bool> uiReady_{false};

  // Sender preparation.
  bool prepareSend();
  bool preparePokemonSection();
  bool prepareStatsSection();
  bool prepareBookSection();
  // Receiver application.
  bool applyReceivedContainer();
  void applyStatsSection(const nearby_sync::ContainerHeader& header);
  void applyBookSection(const nearby_sync::ContainerHeader& header);
  bool stagePokemonSection(const nearby_sync::ContainerHeader& header);
  void buildResult(bool ok);

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
  bool finishReceived(uint64_t expectedBytes, uint32_t expectedCrc);
  void finishSend(const nearby_sync::Result& result);
  void cancelTransfer();
  void setState(State state);
  void setError(const char* message);
  void exitAfterRadio();
  void openReceivedBook();
  void updateTimers();
  void maybeRefreshProgress();
  bool isMenuState() const;
  int menuItemCount() const;
  const char* menuLabel(int index) const;
  void activateSelected();
  void updateNavigation();
  int successActionCount() const;
  const char* title() const;
  static void menuScreen(UiApp::ScreenType& screen, void* user);
  static void onRowEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildMenuScreen(UiApp::ScreenType& screen);
};
