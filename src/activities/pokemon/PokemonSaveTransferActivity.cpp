#if defined(CROSSINK_ENABLE_POKEMON)

#include "PokemonSaveTransferActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <PokemonSpecies.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "components/TouchActionButtons.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "pokemon/PokemonService.h"

#if defined(ARDUINO_ARCH_ESP32) && !defined(SIMULATOR)
#include <esp_system.h>
#endif

namespace nearby = freeink::nearby;
namespace fui = freeink::ui;
namespace st = pokemon::save_transfer;

namespace {
constexpr const char* LOG_TAG = "PKXFER";
constexpr uint8_t BROADCAST_MAC[nearby::MAC_BYTES] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
constexpr uint8_t RESULT_OK = 0;
constexpr uint8_t RESULT_FAILED = 1;
constexpr uint8_t REJECT_USER = 1;
constexpr uint8_t REJECT_STORAGE = 2;
constexpr uint8_t REJECT_VERSION = 3;

TouchActionButtons::Layout touchActionLayout(const Rect& screen, const uint8_t count) {
  constexpr int sideMargin = 24;
  constexpr int bottomMargin = 12;
  const int totalHeight =
      TouchActionButtons::kDefaultHeight * count + TouchActionButtons::kDefaultGap * (count > 0 ? count - 1 : 0);
  return TouchActionButtons::vertical(Rect{screen.x + sideMargin, screen.y + screen.height - bottomMargin - totalHeight,
                                           std::max(1, screen.width - sideMargin * 2), totalHeight},
                                      count);
}

bool sameMac(const std::array<uint8_t, nearby::MAC_BYTES>& lhs, const uint8_t* rhs) {
  return rhs && memcmp(lhs.data(), rhs, nearby::MAC_BYTES) == 0;
}

template <size_t N>
void copyDeviceName(std::array<char, N>& out, const char* name, const size_t length) {
  out.fill('\0');
  memcpy(out.data(), name, std::min(length, N - 1));
}
}  // namespace

PokemonSaveTransferActivity::PokemonSaveTransferActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                         const Mode mode)
    : Activity("PokemonSaveTransfer", renderer, mappedInput),
      mode_(mode),
      uiTarget_(makeUiTarget(renderer)),
      app_(uiTarget_, uiTarget_.deviceContext()) {}

PokemonSaveTransferActivity::~PokemonSaveTransferActivity() { transport_.end(); }

void PokemonSaveTransferActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  uiReady_ = false;
  applySharedUiTheme(app_, uiTarget_);
  app_.on(ACTION_ROW, &PokemonSaveTransferActivity::onRowEvent, this);
  app_.setScreen(&PokemonSaveTransferActivity::menuScreen, this);

  if (mode_ == Mode::Receive) {
    // A transfer committed earlier but never installed must finish before a
    // new one may stage over it (commitStagedBundle() refuses otherwise).
    if (!st::applyPendingSaveTransfer()) {
      setError(tr(STR_POKEMON_TRANSFER_INSTALL_PENDING));
      return;
    }
    startListening();
    return;
  }
  if (!st::hasLocalSave()) {
    setError(tr(STR_POKEMON_TRANSFER_NO_SAVE));
    return;
  }
  setState(State::ChooseSendMode);
}

void PokemonSaveTransferActivity::onExit() {
  source_.close();
  receiveFile_.close();
  transport_.end();
  Activity::onExit();
}

bool PokemonSaveTransferActivity::skipLoopDelay() {
  return state_ == State::Discovering || state_ == State::WaitingForApproval || state_ == State::Sending ||
         state_ == State::Receiving || state_ == State::Listening;
}

bool PokemonSaveTransferActivity::startRadio() {
  if (transport_.started()) return true;
  radioUsed_ = true;
  if (!transport_.begin(ESPNOW_CHANNEL)) {
    setError(tr(STR_NEARBY_TRANSFER_RADIO_FAILED));
    return false;
  }
  return true;
}

// Summary of this device's save for the receiver's confirmation prompt, plus
// the bundle's size. The bundle itself is scanned (and checksummed) here.
bool PokemonSaveTransferActivity::fillOffer() {
  if (!source_.open()) return false;
  pokemon::PokemonSnapshot snapshot;
  if (pokemon::devicePokemonService().loadSnapshot(snapshot) != pokemon::ServiceStatus::Ok) return false;
  offer_ = pokemon::SaveBundleOffer{};
  offer_.totalBytes = source_.totalBytes();
  offer_.chunkBytes = nearby::V2_CHUNK_BYTES;
  offer_.moveSave = moveSave_;
  offer_.versions = source_.header().versions;
  const pokemon::PokemonRecord& leader = snapshot.party[0];
  offer_.summary.leaderSpeciesId = leader.speciesId;
  offer_.summary.leaderLevel = leader.speciesId == 0 ? 0 : pokemon::levelForXp(leader.totalXp);
  offer_.summary.partyCount = snapshot.partyCount;
  uint8_t badges = 0;
  for (int bit = 0; bit < 8; ++bit) badges += (snapshot.state.battleProgress >> bit) & 1U;
  offer_.summary.badgeCount = badges;
  offer_.summary.caughtCount = pokemon::countMarkedSpecies(snapshot.state.caughtSpecies);
  copyDeviceName(offer_.summary.leaderName, leader.nickname.data(),
                 strnlen(leader.nickname.data(), leader.nickname.size()));
  const char* name = SETTINGS.getEffectiveDeviceName();
  copyDeviceName(offer_.senderName, name, strlen(name));
  return true;
}

void PokemonSaveTransferActivity::startListening() {
  if (!startRadio()) return;
  session_.reset();
  peerMac_ = {};
  offer_ = pokemon::SaveBundleOffer{};
  retryCount_ = 0;
  receiverHasSave_ = st::hasLocalSave();
  setState(State::Listening);
}

void PokemonSaveTransferActivity::startDiscovery() {
  if (!fillOffer()) {
    setError(tr(STR_NEARBY_TRANSFER_SOURCE_FAILED));
    return;
  }
  if (!startRadio()) return;
#if defined(ARDUINO_ARCH_ESP32) && !defined(SIMULATOR)
  sessionId_ = esp_random();
#else
  sessionId_ = static_cast<uint32_t>(millis()) ^ 0x504b5342u;
#endif
  if (sessionId_ == 0) sessionId_ = 1;
  peerCount_ = 0;
  setState(State::Discovering);
  sendDiscovery();
}

bool PokemonSaveTransferActivity::sendPacket(const nearby::PacketType type, const uint8_t* destination,
                                             const uint32_t sequence, const void* payload,
                                             const uint16_t payloadLength) {
  size_t length = 0;
  if (!nearby::encodePacket(packetBuffer_.data(), packetBuffer_.size(), type, sessionId_, sequence, payload,
                            payloadLength, length)) {
    return false;
  }
  return transport_.send(destination, packetBuffer_.data(), length);
}

bool PokemonSaveTransferActivity::sendDiscovery() {
  lastActionMs_ = millis();
  return sendPacket(nearby::PacketType::Discover, BROADCAST_MAC, 0, pokemon::SAVE_BUNDLE_TAG.data(),
                    static_cast<uint16_t>(pokemon::SAVE_BUNDLE_TAG.size()));
}

bool PokemonSaveTransferActivity::sendAdvertisement(const uint8_t* destination) {
  const char* name = SETTINGS.getEffectiveDeviceName();
  const size_t nameLength = std::min<size_t>(strlen(name), sizeof(Peer::name) - 1);
  std::array<uint8_t, 4 + sizeof(Peer::name)> payload{};
  memcpy(payload.data(), pokemon::SAVE_BUNDLE_TAG.data(), 4);
  memcpy(payload.data() + 4, name, nameLength);
  return sendPacket(nearby::PacketType::Advertise, destination, 0, payload.data(),
                    static_cast<uint16_t>(4 + nameLength));
}

void PokemonSaveTransferActivity::selectPeer() {
  if (peerCount_ == 0 || selectedIndex_ < 0 || selectedIndex_ >= peerCount_) return;
  selectedPeer_ = selectedIndex_;
  peerMac_ = peers_[selectedPeer_].mac;
  retryCount_ = 0;
  setState(State::WaitingForApproval);
  sendOffer();
}

bool PokemonSaveTransferActivity::sendOffer() {
  std::array<uint8_t, pokemon::SAVE_BUNDLE_OFFER_MAX_BYTES> payload{};
  size_t length = 0;
  if (!pokemon::encodeSaveBundleOffer(offer_, payload.data(), payload.size(), length)) return false;
  lastActionMs_ = millis();
  return sendPacket(nearby::PacketType::Offer, peerMac_.data(), 0, payload.data(), static_cast<uint16_t>(length));
}

void PokemonSaveTransferActivity::processPackets() {
  while (transport_.poll(eventBuffer_)) {
    nearby::PacketView packet;
    if (!nearby::decodePacket(eventBuffer_.data.data(), eventBuffer_.length, packet)) continue;
    if (mode_ == Mode::Send) {
      handleSenderPacket(eventBuffer_, packet);
    } else {
      handleReceiverPacket(eventBuffer_, packet);
    }
  }
}

void PokemonSaveTransferActivity::handleSenderPacket(const nearby::EspNowTransport::Event& event,
                                                     const nearby::PacketView& packet) {
  if (packet.sessionId != sessionId_) return;
  if (packet.type == nearby::PacketType::Advertise && (state_ == State::Discovering || state_ == State::DeviceList)) {
    // Book-transfer and stats-sync listeners answer every Discover too; only
    // a tagged advertisement comes from a Pokemon save receiver.
    if (!pokemon::hasSaveBundleTag(packet.payload, packet.payloadLength) || packet.payloadLength < 5 ||
        packet.payloadLength > 4 + sizeof(Peer::name) - 1) {
      return;
    }
    for (uint8_t i = 0; i < peerCount_; ++i) {
      if (sameMac(peers_[i].mac, event.sourceMac.data())) return;
    }
    if (peerCount_ >= MAX_PEERS) return;
    Peer& peer = peers_[peerCount_++];
    peer.mac = event.sourceMac;
    copyDeviceName(peer.name, reinterpret_cast<const char*>(packet.payload + 4), packet.payloadLength - 4);
    if (state_ == State::Discovering) {
      setState(State::DeviceList);
    } else {
      requestUpdate();
    }
    return;
  }
  if (!sameMac(peerMac_, event.sourceMac.data())) return;
  if (packet.type == nearby::PacketType::Accept && state_ == State::WaitingForApproval && packet.payloadLength == 2) {
    negotiatedChunkBytes_ =
        std::clamp<uint16_t>(nearby::readU16(packet.payload), nearby::COMPAT_CHUNK_BYTES, nearby::V2_CHUNK_BYTES);
    // Re-scan: the bundle streams straight from the save files, so start from
    // the top (and re-checksum in case anything changed while waiting).
    if (!source_.open() || source_.totalBytes() != offer_.totalBytes) {
      setError(tr(STR_NEARBY_TRANSFER_SOURCE_FAILED));
      return;
    }
    session_.begin(nearby::ReliableTransferSession::Role::Sender, sessionId_, offer_.totalBytes,
                   negotiatedChunkBytes_);
    retryCount_ = 0;
    setState(State::Sending);
    sendNextChunk();
    return;
  }
  if (packet.type == nearby::PacketType::Reject && state_ == State::WaitingForApproval) {
    const bool version = packet.payloadLength == 1 && packet.payload[0] == REJECT_VERSION;
    const bool storage = packet.payloadLength == 1 && packet.payload[0] == REJECT_STORAGE;
    setError(version ? tr(STR_POKEMON_TRANSFER_INCOMPATIBLE)
                     : (storage ? tr(STR_NEARBY_TRANSFER_NO_SPACE) : tr(STR_NEARBY_TRANSFER_REJECTED)));
    return;
  }
  if (packet.type == nearby::PacketType::Ack && state_ == State::Sending && packet.payloadLength == 4) {
    if (!session_.acceptAcknowledgement(nearby::readU32(packet.payload))) return;
    session_.advanceSentBytes(pendingChunkLength_);
    pendingChunkLength_ = 0;
    retryCount_ = 0;
    maybeRefreshProgress();
    if (session_.transferredBytes() == session_.totalBytes()) {
      source_.close();
      sendComplete();
    } else {
      sendNextChunk();
    }
    return;
  }
  if (packet.type == nearby::PacketType::Result && state_ == State::Sending && packet.payloadLength == 1) {
    if (packet.payload[0] == RESULT_OK) {
      finishSend();
    } else {
      setError(tr(STR_NEARBY_TRANSFER_VERIFY_FAILED));
    }
    return;
  }
  if (packet.type == nearby::PacketType::Cancel) setError(tr(STR_NEARBY_TRANSFER_CANCELLED));
}

void PokemonSaveTransferActivity::handleReceiverPacket(const nearby::EspNowTransport::Event& event,
                                                       const nearby::PacketView& packet) {
  if (packet.type == nearby::PacketType::Discover && state_ == State::Listening) {
    if (!pokemon::hasSaveBundleTag(packet.payload, packet.payloadLength)) return;
    sessionId_ = packet.sessionId;
    sendAdvertisement(event.sourceMac.data());
    return;
  }
  if (packet.type == nearby::PacketType::Offer && state_ == State::Listening) {
    pokemon::SaveBundleOffer offer;
    if (!pokemon::decodeSaveBundleOffer(packet.payload, packet.payloadLength, offer)) return;
    sessionId_ = packet.sessionId;
    peerMac_ = event.sourceMac;
    offer_ = offer;
    negotiatedChunkBytes_ =
        std::clamp<uint16_t>(offer.chunkBytes, nearby::COMPAT_CHUNK_BYTES, nearby::V2_CHUNK_BYTES);
    if (!pokemon::saveFormatCompatible(offer.versions, pokemon::currentSaveFormatVersions())) {
      const uint8_t reason = REJECT_VERSION;
      sendPacket(nearby::PacketType::Reject, peerMac_.data(), 0, &reason, 1);
      setError(tr(STR_POKEMON_TRANSFER_INCOMPATIBLE));
      return;
    }
    setState(State::OfferPrompt);
    return;
  }
  if (packet.sessionId != sessionId_ || !sameMac(peerMac_, event.sourceMac.data())) return;
  if (packet.type == nearby::PacketType::Offer && state_ == State::Receiving) {
    // Our Accept was lost: repeat it.
    uint8_t payload[2];
    nearby::writeU16(payload, negotiatedChunkBytes_);
    sendPacket(nearby::PacketType::Accept, peerMac_.data(), 0, payload, sizeof(payload));
    return;
  }
  if (packet.type == nearby::PacketType::Data && state_ == State::Receiving) {
    if (packet.sequence != session_.nextSequence() || packet.payloadLength == 0 ||
        packet.payloadLength > negotiatedChunkBytes_) {
      sendAck();  // duplicate or out of order: re-announce what we expect
      return;
    }
    const size_t written = receiveFile_.write(packet.payload, packet.payloadLength);
    if (written != packet.payloadLength ||
        !session_.acceptReceivedChunk(packet.sequence, static_cast<size_t>(packet.payloadLength))) {
      sendResult(false);
      setError(tr(STR_NEARBY_TRANSFER_WRITE_FAILED));
      return;
    }
    session_.includeBytes(packet.payload, packet.payloadLength);
    sendAck();
    maybeRefreshProgress();
    return;
  }
  if (packet.type == nearby::PacketType::Complete && packet.payloadLength == 12) {
    if (state_ == State::Success || state_ == State::Installing) {
      sendResult(true);  // the sender missed our first Result
      return;
    }
    if (state_ != State::Receiving) return;
    const bool ok = finishReceivedBundle(nearby::readU64(packet.payload), nearby::readU32(packet.payload + 8));
    sendResult(ok);
    if (ok) installReceivedBundle();
    return;
  }
  if (packet.type == nearby::PacketType::Cancel && (state_ == State::Receiving || state_ == State::OfferPrompt)) {
    setError(tr(STR_NEARBY_TRANSFER_CANCELLED));
  }
}

void PokemonSaveTransferActivity::acceptOffer() {
  uint64_t total = 0;
  uint64_t used = 0;
#ifndef SIMULATOR
  total = Storage.totalBytes();
  used = Storage.usedBytes();
#endif
  // Room for the staged bundle plus the installed copy of it.
  if (total > 0 && used <= total && 2ULL * offer_.totalBytes > total - used) {
    const uint8_t reason = REJECT_STORAGE;
    sendPacket(nearby::PacketType::Reject, peerMac_.data(), 0, &reason, 1);
    setError(tr(STR_NEARBY_TRANSFER_NO_SPACE));
    return;
  }
  st::discardStagedBundle();
  receiveFile_ = Storage.open(st::STAGING_PATH, O_WRONLY | O_CREAT | O_TRUNC);
  if (!receiveFile_) {
    setError(tr(STR_NEARBY_TRANSFER_WRITE_FAILED));
    return;
  }
  session_.begin(nearby::ReliableTransferSession::Role::Receiver, sessionId_, offer_.totalBytes,
                 negotiatedChunkBytes_);
  retryCount_ = 0;
  setState(State::Receiving);
  // Draw the progress screen before any data arrives: a full e-ink refresh in
  // the middle of the transfer would stall the loop long enough for the
  // sender to start retrying.
  if (requestUpdateAndWait() != RequestUpdateResult::Rendered) requestUpdate(true);
  uint8_t payload[2];
  nearby::writeU16(payload, negotiatedChunkBytes_);
  if (!sendPacket(nearby::PacketType::Accept, peerMac_.data(), 0, payload, sizeof(payload))) {
    setError(tr(STR_NEARBY_TRANSFER_RADIO_FAILED));
    return;
  }
  lastActionMs_ = millis();
}

void PokemonSaveTransferActivity::rejectOffer() {
  const uint8_t reason = REJECT_USER;
  sendPacket(nearby::PacketType::Reject, peerMac_.data(), 0, &reason, 1);
  startListening();
}

bool PokemonSaveTransferActivity::sendNextChunk() {
  pendingChunkLength_ = 0;
  const int bytesRead = source_.read(chunkBuffer_.data(), negotiatedChunkBytes_);
  if (bytesRead <= 0) {
    setError(tr(STR_NEARBY_TRANSFER_SOURCE_FAILED));
    return false;
  }
  pendingChunkLength_ = static_cast<size_t>(bytesRead);
  session_.includeBytes(chunkBuffer_.data(), pendingChunkLength_);
  retryCount_ = 0;
  return resendPending();
}

bool PokemonSaveTransferActivity::resendPending() {
  lastActionMs_ = millis();
  return sendPacket(nearby::PacketType::Data, peerMac_.data(), session_.nextSequence(), chunkBuffer_.data(),
                    static_cast<uint16_t>(pendingChunkLength_));
}

bool PokemonSaveTransferActivity::sendAck() {
  uint8_t payload[4];
  nearby::writeU32(payload, session_.nextSequence());
  lastActionMs_ = millis();
  return sendPacket(nearby::PacketType::Ack, peerMac_.data(), 0, payload, sizeof(payload));
}

bool PokemonSaveTransferActivity::sendComplete() {
  uint8_t payload[12];
  nearby::writeU64(payload, session_.totalBytes());
  nearby::writeU32(payload + 8, session_.crc32());
  lastActionMs_ = millis();
  return sendPacket(nearby::PacketType::Complete, peerMac_.data(), 0, payload, sizeof(payload));
}

void PokemonSaveTransferActivity::sendResult(const bool ok) {
  const uint8_t result = ok ? RESULT_OK : RESULT_FAILED;
  sendPacket(nearby::PacketType::Result, peerMac_.data(), 0, &result, 1);
}

// Transport-level check (byte count + whole-stream CRC), then the bundle's
// own checks, then the journal commit. Only after that commit does the sender
// hear "OK" - so a sender that moves its save never deletes it before the
// receiver is guaranteed to end up with it.
bool PokemonSaveTransferActivity::finishReceivedBundle(const uint64_t expectedBytes, const uint32_t expectedCrc) {
  const bool synced = receiveFile_.sync();
  const bool closed = receiveFile_.close();
  if (expectedBytes != session_.totalBytes() || expectedBytes != session_.transferredBytes() ||
      expectedCrc != session_.crc32()) {
    st::discardStagedBundle();
    setError(tr(STR_NEARBY_TRANSFER_VERIFY_FAILED));
    return false;
  }
  if (!synced || !closed) {
    st::discardStagedBundle();
    setError(tr(STR_NEARBY_TRANSFER_WRITE_FAILED));
    return false;
  }
  pokemon::SaveBundleHeader header;
  const st::VerifyResult verified = st::verifyStagedBundle(header);
  if (verified != st::VerifyResult::Ok) {
    st::discardStagedBundle();
    setError(verified == st::VerifyResult::UnsupportedVersion ? tr(STR_POKEMON_TRANSFER_INCOMPATIBLE)
                                                              : tr(STR_NEARBY_TRANSFER_VERIFY_FAILED));
    return false;
  }
  if (!st::commitStagedBundle()) {
    st::discardStagedBundle();
    setError(tr(STR_NEARBY_TRANSFER_WRITE_FAILED));
    return false;
  }
  return true;
}

void PokemonSaveTransferActivity::installReceivedBundle() {
  setState(State::Installing);
  if (requestUpdateAndWait() != RequestUpdateResult::Rendered) requestUpdate(true);
  // Already committed: a failure here is finished by the next boot's
  // devicePokemonService(), so it is still a success for the player.
  installPending_ = !st::applyPendingSaveTransfer();
  if (installPending_) LOG_ERR(LOG_TAG, "Install deferred to next boot");
  setState(State::Success);
}

void PokemonSaveTransferActivity::finishSend() {
  if (moveSave_ && !st::moveLocalSaveToBackup()) {
    setError(tr(STR_POKEMON_TRANSFER_MOVE_FAILED));
    return;
  }
  setState(State::Success);
}

void PokemonSaveTransferActivity::cancelTransfer() {
  if (transport_.started() && peerMac_ != std::array<uint8_t, nearby::MAC_BYTES>{} &&
      (state_ == State::Sending || state_ == State::Receiving || state_ == State::WaitingForApproval ||
       state_ == State::OfferPrompt)) {
    sendPacket(nearby::PacketType::Cancel, peerMac_.data());
  }
  source_.close();
  if (receiveFile_) {
    receiveFile_.close();
    st::discardStagedBundle();
  }
}

void PokemonSaveTransferActivity::setState(const State state) {
  state_ = state;
  selectedIndex_ = 0;
  uiReady_ = false;
  app_.clearTapFlash();
  lastUiMs_ = millis();
  requestUpdate();
}

void PokemonSaveTransferActivity::setError(const char* message) {
  errorMessage_ = message ? message : tr(STR_NEARBY_TRANSFER_FAILED);
  LOG_ERR(LOG_TAG, "%s", errorMessage_.c_str());
  cancelTransfer();
  setState(State::Error);
}

// The radio leaves Wi-Fi in a state the rest of the firmware does not expect,
// and a received or moved save must be reloaded from SD anyway: restart.
void PokemonSaveTransferActivity::exitAfterRadio() {
  cancelTransfer();
  transport_.end();
  if (radioUsed_) {
    silentRestart();
  } else {
    finish();
  }
}

void PokemonSaveTransferActivity::updateTimers() {
  const uint32_t now = millis();
  if (state_ == State::Discovering && now - lastActionMs_ >= DISCOVERY_INTERVAL_MS) {
    sendDiscovery();
    return;
  }
  if (state_ == State::WaitingForApproval && now - lastActionMs_ >= RETRY_INTERVAL_MS * 2) {
    if (++retryCount_ > MAX_APPROVAL_RETRIES) {
      setError(tr(STR_NEARBY_TRANSFER_TIMEOUT));
    } else {
      sendOffer();
    }
    return;
  }
  if (state_ == State::Receiving && now - lastActionMs_ >= RECEIVE_TIMEOUT_MS) {
    setError(tr(STR_NEARBY_TRANSFER_TIMEOUT));
    return;
  }
  if (state_ != State::Sending || now - lastActionMs_ < RETRY_INTERVAL_MS) return;
  if (++retryCount_ > MAX_RETRIES) {
    setError(tr(STR_NEARBY_TRANSFER_TIMEOUT));
    return;
  }
  if (pendingChunkLength_ > 0) {
    resendPending();
  } else {
    sendComplete();
  }
}

void PokemonSaveTransferActivity::maybeRefreshProgress() {
  if (millis() - lastUiMs_ < UI_REFRESH_MS) return;
  lastUiMs_ = millis();
  requestUpdate();
}

bool PokemonSaveTransferActivity::isMenuState() const {
  return state_ == State::ChooseSendMode || state_ == State::DeviceList;
}

int PokemonSaveTransferActivity::menuItemCount() const {
  if (state_ == State::ChooseSendMode) return 2;
  if (state_ == State::DeviceList) return peerCount_;
  return 0;
}

void PokemonSaveTransferActivity::activateSelected() {
  switch (state_) {
    case State::ChooseSendMode:
      moveSave_ = selectedIndex_ == 1;
      startDiscovery();
      break;
    case State::DeviceList:
      selectPeer();
      break;
    case State::OfferPrompt:
      acceptOffer();
      break;
    case State::Success:
    case State::Error:
      exitAfterRadio();
      break;
    default:
      break;
  }
}

void PokemonSaveTransferActivity::menuScreen(UiApp::ScreenType& screen, void* user) {
  static_cast<PokemonSaveTransferActivity*>(user)->buildMenuScreen(screen);
}

void PokemonSaveTransferActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<PokemonSaveTransferActivity*>(user);
  if (!self->isMenuState() || event.value < 0 || event.value >= self->menuItemCount()) return;
  self->selectedIndex_ = event.value;
  self->app_.clearTapFlash();
  self->activateSelected();
}

void PokemonSaveTransferActivity::buildMenuScreen(UiApp::ScreenType& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // ChooseSendMode keeps room above the list for its question.
  const int16_t extraTop = state_ == State::ChooseSendMode ? 48 : 0;
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(metrics.topPadding + TouchHeaderBackButton::height(metrics, mappedInput) +
                                       metrics.verticalSpacing + extraTop),
                  0, static_cast<int16_t>(metrics.buttonHintsHeight + metrics.verticalSpacing), 0});

  std::array<fui::ListItem, MAX_PEERS> items{};
  const int count = menuItemCount();
  for (int i = 0; i < count; ++i) {
    if (state_ == State::ChooseSendMode) {
      items[i].label = i == 0 ? tr(STR_POKEMON_TRANSFER_COPY) : tr(STR_POKEMON_TRANSFER_MOVE);
    } else {
      items[i].label = peers_[i].name.data();
    }
    items[i].actionValue = static_cast<int16_t>(i);
  }

  fui::ListProps props;
  props.items = items.data();
  props.count = static_cast<uint16_t>(count);
  props.selectedIndex = static_cast<int16_t>(selectedIndex_);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 2;
  configureUiList(props, screen.theme(), screen.body());
  screen.list(props);
}

void PokemonSaveTransferActivity::updateNavigation() {
  const int count = menuItemCount();
  if (count <= 1) return;
  buttonNavigator_.onNextRelease([this, count] {
    selectedIndex_ = ButtonNavigator::nextIndex(selectedIndex_, count);
    requestUpdate();
  });
  buttonNavigator_.onPreviousRelease([this, count] {
    selectedIndex_ = ButtonNavigator::previousIndex(selectedIndex_, count);
    requestUpdate();
  });
}

void PokemonSaveTransferActivity::loop() {
  processPackets();
  updateTimers();

  if (TouchHeaderBackButton::wasTapped(mappedInput, renderer) ||
      mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (state_ == State::Installing) return;  // never interrupt the install itself
    if (state_ == State::OfferPrompt) {
      rejectOffer();
    } else {
      exitAfterRadio();
    }
    return;
  }

  int tx = 0;
  int ty = 0;
  if (mappedInput.hasTouch() && (state_ == State::OfferPrompt || state_ == State::Success || state_ == State::Error) &&
      mappedInput.wasScreenTouchDown(tx, ty)) {
    const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const uint8_t actionCount = state_ == State::OfferPrompt ? 2 : 1;
    const int action = TouchActionButtons::indexAt(touchActionLayout(safeArea, actionCount), tx, ty);
    if (action == 0) {
      activateSelected();
    } else if (action == 1) {
      rejectOffer();
    }
    if (action >= 0) return;
  }

  if (isMenuState() && uiReady_) {
    const fui::InputSnapshot snap = touchSnapshotFrom(mappedInput);
    if (snap.touchPressed || snap.touchReleased) {
      const auto event = app_.route(snap);
      if (app_.invalidated()) requestUpdate();
      if (event) return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelected();
    return;
  }
  updateNavigation();
}

void PokemonSaveTransferActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const Rect header{0, metrics.topPadding, width, TouchHeaderBackButton::height(metrics, mappedInput)};
  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::draw(renderer, uiTarget_, header, tr(STR_POKEMON_SAVE_TRANSFER), false);
  } else {
    GUI.drawHeader(renderer, header, tr(STR_POKEMON_SAVE_TRANSFER));
  }

  const Rect textArea{metrics.contentSidePadding, 0, width - metrics.contentSidePadding * 2, height};
  auto title = [this, height, textArea](const char* text, const int offset = 0) {
    UITheme::drawCenteredWrappedTextAtCenter(renderer, textArea, UI_10_FONT_ID, height / 2 + offset, text, 2, true,
                                             EpdFontFamily::BOLD);
  };
  auto note = [this, height, textArea](const char* text, const int offset, const int maxLines = 3) {
    UITheme::drawCenteredWrappedTextAtCenter(renderer, textArea, SMALL_FONT_ID, height / 2 + offset, text, maxLines);
  };
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  uiReady_ = false;
  if (isMenuState()) {
    if (state_ == State::ChooseSendMode) {
      const int questionY = metrics.topPadding + header.height + metrics.verticalSpacing + 24;
      UITheme::drawCenteredWrappedTextAtCenter(renderer, textArea, UI_10_FONT_ID, questionY,
                                               tr(STR_POKEMON_TRANSFER_MODE_QUESTION), 2, true, EpdFontFamily::BOLD);
    }
    app_.render();
    uiReady_ = true;
  } else if (state_ == State::Listening) {
    title(tr(STR_NEARBY_TRANSFER_LISTENING));
  } else if (state_ == State::Discovering) {
    title(tr(STR_NEARBY_TRANSFER_DISCOVERING));
  } else if (state_ == State::WaitingForApproval) {
    title(tr(STR_NEARBY_TRANSFER_WAITING_APPROVAL), -10);
    note(peers_[selectedPeer_].name.data(), lineHeight + 18, 2);
  } else if (state_ == State::OfferPrompt) {
    title(tr(STR_POKEMON_TRANSFER_INCOMING), -110);
    note(offer_.senderName.data(), -70, 2);
    const pokemon::SpeciesData* species = pokemon::speciesData(offer_.summary.leaderSpeciesId);
    const char* leader = offer_.summary.leaderName[0] != '\0' ? offer_.summary.leaderName.data()
                         : species != nullptr                ? species->name
                                                             : "???";
    char summary[128];
    snprintf(summary, sizeof(summary), tr(STR_POKEMON_TRANSFER_SUMMARY), leader,
             static_cast<unsigned>(offer_.summary.leaderLevel), static_cast<unsigned>(offer_.summary.badgeCount),
             static_cast<unsigned>(offer_.summary.caughtCount));
    title(summary, -20);
    if (receiverHasSave_) note(tr(STR_POKEMON_TRANSFER_REPLACE_WARNING), 40);
    if (offer_.moveSave) note(tr(STR_POKEMON_TRANSFER_MOVE_NOTE), 110, 2);
  } else if (state_ == State::Sending || state_ == State::Receiving) {
    title(state_ == State::Sending ? tr(STR_POKEMON_TRANSFER_SENDING) : tr(STR_POKEMON_TRANSFER_RECEIVING), -45);
    GUI.drawProgressBar(renderer,
                        Rect{metrics.contentSidePadding, height / 2 + 10, width - metrics.contentSidePadding * 2,
                             metrics.progressBarHeight},
                        static_cast<size_t>(session_.transferredBytes()), static_cast<size_t>(session_.totalBytes()));
  } else if (state_ == State::Installing) {
    title(tr(STR_NEARBY_TRANSFER_VALIDATING));
  } else if (state_ == State::Success) {
    if (mode_ == Mode::Receive) {
      title(tr(STR_POKEMON_TRANSFER_RECEIVED));
      note(installPending_ ? tr(STR_POKEMON_TRANSFER_INSTALL_PENDING) : tr(STR_POKEMON_TRANSFER_RESTART_NOTE),
           lineHeight + 10);
    } else {
      title(tr(STR_POKEMON_TRANSFER_SENT));
      if (moveSave_) note(tr(STR_POKEMON_TRANSFER_MOVED), lineHeight + 10);
    }
  } else if (state_ == State::Error) {
    title(tr(STR_NEARBY_TRANSFER_FAILED));
    note(errorMessage_.c_str(), lineHeight + 10);
  }

  const char* back = state_ == State::Installing ? "" : tr(STR_CANCEL);
  const char* confirm = "";
  if (isMenuState()) {
    confirm = tr(STR_SELECT);
  } else if (state_ == State::OfferPrompt) {
    confirm = tr(STR_ACCEPT);
  } else if (state_ == State::Success || state_ == State::Error) {
    back = "";
    confirm = tr(STR_OK);
  }
  const bool touchActions = mappedInput.hasTouch() &&
                            (state_ == State::OfferPrompt || state_ == State::Success || state_ == State::Error);
  if (touchActions) {
    const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const uint8_t actionCount = state_ == State::OfferPrompt ? 2 : 1;
    const char* actionLabels[] = {confirm, actionCount == 2 ? back : nullptr};
    TouchActionButtons::draw(renderer, touchActionLayout(safeArea, actionCount), actionLabels, 0, -1, UI_10_FONT_ID);
  } else {
    const bool showNavigation = !mappedInput.hasTouch() && menuItemCount() > 1;
    const auto labels = mappedInput.mapLabels(back, confirm, showNavigation ? tr(STR_DIR_UP) : "",
                                              showNavigation ? tr(STR_DIR_DOWN) : "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer(screenTransitionRefresh_.modeFor(static_cast<uint8_t>(state_)));
}

#endif
