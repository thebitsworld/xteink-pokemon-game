#include "NearbySyncActivity.h"

#include <Arduino.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "SilentRestart.h"
#include "activities/home/RecentBookProgress.h"
#include "activities/reader/GlobalReadingStats.h"
#include "components/TouchActionButtons.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "fontIds.h"

#if defined(CROSSINK_ENABLE_POKEMON)
#include <PokemonSaveBundleCodec.h>
#include <PokemonSpecies.h>

#include "pokemon/PokemonSaveTransfer.h"
#include "pokemon/PokemonService.h"
#endif

#if defined(ARDUINO_ARCH_ESP32) && !defined(SIMULATOR)
#include <esp_mac.h>
#include <esp_system.h>
#endif

namespace nearby = freeink::nearby;
namespace fui = freeink::ui;
namespace ns = nearby_sync;

namespace {
constexpr const char* LOG_TAG = "NSYNC";
constexpr const char* CACHE_ROOT = "/.crosspoint";
constexpr const char* GLOBAL_STATS_PATH = "/.crosspoint/global_stats.bin";
constexpr uint8_t BROADCAST_MAC[nearby::MAC_BYTES] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
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
void copyText(std::array<char, N>& out, const char* text, const size_t length) {
  out.fill('\0');
  memcpy(out.data(), text, std::min(length, N - 1));
}

// This reader's own global_stats.bin, made sure to exist first (same as the
// Sync Stats screen does).
bool readLocalStats(uint8_t* output, size_t& length) {
  length = 0;
  GlobalReadingStats::load().save();
  HalFile file = Storage.open(GLOBAL_STATS_PATH, O_RDONLY);
  if (!file) return false;
  const size_t size = static_cast<size_t>(file.fileSize64());
  const bool ok = size <= ns::MAX_STATS_BYTES && file.read(output, size) == static_cast<int>(size) &&
                  ns::isValidStatsPayload(output, size);
  file.close();
  if (ok) length = size;
  return ok;
}

// progress.bin exactly as the reader wrote it (or its backup).
bool readRawProgress(const std::string& cachePath, uint8_t* output, uint8_t& length) {
  length = 0;
  for (const char* suffix : {"/progress.bin", "/progress.bin.bak"}) {
    HalFile file = Storage.open((cachePath + suffix).c_str(), O_RDONLY);
    if (!file) continue;
    const size_t size = static_cast<size_t>(file.fileSize64());
    const bool ok = (size == 4 || size == 6 || size == 10) && file.read(output, size) == static_cast<int>(size);
    file.close();
    if (ok) {
      length = static_cast<uint8_t>(size);
      return true;
    }
  }
  return false;
}

std::string joinPath(const std::string& folder, const char* name) {
  if (folder.empty() || folder == "/") return std::string("/") + name;
  return folder.back() == '/' ? folder + name : folder + "/" + name;
}

// Where the sender's book lives on this reader: the same path, or a file with
// the same name among the recent books, in the nearby-transfer receive folder
// (where Nearby File Transfer puts books), or at the SD root.
std::string locateBook(const char* senderPath) {
  if (Storage.exists(senderPath)) return senderPath;
  const char* name = ns::baseName(senderPath);
  for (const RecentBook& recent : RECENT_BOOKS.getBooks()) {
    if (strcmp(ns::baseName(recent.path.c_str()), name) == 0 && Storage.exists(recent.path.c_str())) {
      return recent.path;
    }
  }
  for (const std::string& folder : {std::string(SETTINGS.nearbyReceiveFolder), std::string("/")}) {
    const std::string candidate = joinPath(folder, name);
    if (Storage.exists(candidate.c_str())) return candidate;
  }
  return {};
}

#if defined(CROSSINK_ENABLE_POKEMON)
namespace st = pokemon::save_transfer;

class PokemonBundleStream final : public ns::SectionStream {
 public:
  bool restart() override { return source_.open(); }
  int read(uint8_t* output, const size_t capacity) override { return source_.read(output, capacity); }
  ~PokemonBundleStream() override { source_.close(); }

 private:
  st::BundleSource source_;
};

pokemon::SaveFormatVersions versionsFrom(const ns::PokemonOfferSummary& summary) {
  pokemon::SaveFormatVersions versions;
  versions.snapshot = summary.snapshotVersion;
  versions.battle = summary.storeVersions[0];
  versions.ivEv = summary.storeVersions[1];
  versions.moveset = summary.storeVersions[2];
  versions.hallOfFame = summary.storeVersions[3];
  return versions;
}
#endif
}  // namespace

NearbySyncActivity::NearbySyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const Role role,
                                       const Scope scope)
    : Activity("NearbySync", renderer, mappedInput),
      role_(role),
      scope_(scope),
      uiTarget_(makeUiTarget(renderer)),
      app_(uiTarget_, uiTarget_.deviceContext()) {}

NearbySyncActivity::~NearbySyncActivity() { transport_.end(); }

void NearbySyncActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  uiReady_ = false;
  applySharedUiTheme(app_, uiTarget_);
  app_.on(ACTION_ROW, &NearbySyncActivity::onRowEvent, this);
  app_.setScreen(&NearbySyncActivity::menuScreen, this);
#if defined(ARDUINO_ARCH_ESP32) && !defined(SIMULATOR)
  esp_efuse_mac_get_default(localMac_.data());
#endif

  if (role_ == Role::Ask) {
    setState(State::ChooseRole);
  } else if (role_ == Role::Send) {
    if (prepareSend()) setState(State::ChooseSendMode);
  } else {
    startListening();
  }
}

void NearbySyncActivity::onExit() {
  receiveFile_.close();
  transport_.end();
  Activity::onExit();
}

bool NearbySyncActivity::skipLoopDelay() {
  return state_ == State::Discovering || state_ == State::WaitingForApproval || state_ == State::Sending ||
         state_ == State::Receiving || state_ == State::Listening;
}

const char* NearbySyncActivity::title() const {
#if defined(CROSSINK_ENABLE_POKEMON)
  if (scope_ == Scope::PokemonOnly) return tr(STR_POKEMON_SAVE_TRANSFER);
#endif
  return tr(STR_NEARBY_FULL_SYNC);
}

// ---- Sender: gathering what to send -------------------------------------

bool NearbySyncActivity::preparePokemonSection() {
#if defined(CROSSINK_ENABLE_POKEMON)
  if (!st::hasLocalSave()) return false;
  pokemon::PokemonSnapshot snapshot;
  if (pokemon::devicePokemonService().loadSnapshot(snapshot) != pokemon::ServiceStatus::Ok) return false;
  auto stream = std::make_unique<PokemonBundleStream>();
  if (!source_.addStreamSection(ns::SectionType::PokemonSave, *stream)) return false;
  pokemonStream_ = std::move(stream);

  const pokemon::SaveFormatVersions versions = pokemon::currentSaveFormatVersions();
  ns::PokemonOfferSummary& summary = offer_.pokemon;
  summary.snapshotVersion = versions.snapshot;
  summary.storeVersions = {versions.battle, versions.ivEv, versions.moveset, versions.hallOfFame};
  const pokemon::PokemonRecord& leader = snapshot.party[0];
  summary.leaderSpeciesId = leader.speciesId;
  summary.leaderLevel = leader.speciesId == 0 ? 0 : pokemon::levelForXp(leader.totalXp);
  summary.partyCount = snapshot.partyCount;
  uint8_t badges = 0;
  for (int bit = 0; bit < 8; ++bit) badges += (snapshot.state.battleProgress >> bit) & 1U;
  summary.badgeCount = badges;
  summary.caughtCount = pokemon::countMarkedSpecies(snapshot.state.caughtSpecies);
  copyText(summary.leaderName, leader.nickname.data(), strnlen(leader.nickname.data(), leader.nickname.size()));
  return true;
#else
  return false;
#endif
}

bool NearbySyncActivity::prepareStatsSection() {
  std::array<uint8_t, ns::MAX_STATS_BYTES> stats{};
  size_t length = 0;
  return readLocalStats(stats.data(), length) &&
         source_.addMemorySection(ns::SectionType::ReadingStats, stats.data(), length);
}

// The book last open on this reader, with its saved position. Only EPUBs: their
// progress.bin carries a text offset that survives a different layout on the
// other reader.
bool NearbySyncActivity::prepareBookSection() {
  const std::string path = APP_STATE.openEpubPath;
  if (path.empty() || path.size() > ns::MAX_BOOK_PATH_BYTES || path[0] != '/' || !FsHelpers::hasEpubExtension(path) ||
      !Storage.exists(path.c_str())) {
    return false;
  }
  book_ = ns::BookPositionRecord{};
  copyText(book_.path, path.c_str(), path.size());
  if (!readRawProgress(Epub::cachePathForFilePath(path, CACHE_ROOT), book_.progress.data(), book_.progressLength)) {
    return false;
  }
  const RecentBook recent = RECENT_BOOKS.getDataFromBook(path);
  const std::string title = recent.title.empty() ? std::string(ns::baseName(path.c_str())) : recent.title;
  copyText(book_.title, title.c_str(), title.size());
  RecentBook lookup;
  lookup.path = path;
  const float percent = RecentBookProgress::loadCachedEpubPercent(lookup);
  book_.percentBasisPoints = RecentBookProgress::hasPercent(percent)
                                 ? static_cast<uint16_t>(std::clamp(percent, 0.0f, 100.0f) * 100.0f + 0.5f)
                                 : ns::UNKNOWN_PERCENT;
  std::array<uint8_t, ns::MAX_BOOK_RECORD_BYTES> bytes{};
  size_t length = 0;
  if (!ns::encodeBookPosition(book_, bytes.data(), bytes.size(), length) ||
      !source_.addMemorySection(ns::SectionType::BookPosition, bytes.data(), length)) {
    return false;
  }
  offer_.bookTitle = book_.title;
  offer_.bookPercentBasisPoints = book_.percentBasisPoints;
  return true;
}

bool NearbySyncActivity::prepareSend() {
  source_ = ns::ContainerSource{};
  pokemonStream_.reset();
  offer_ = ns::Offer{};
  hasPokemon_ = preparePokemonSection();
  if (scope_ == Scope::Everything) {
    hasStats_ = prepareStatsSection();
    hasBook_ = prepareBookSection();
  }
  if (!hasPokemon_ && !hasStats_ && !hasBook_) {
#if defined(CROSSINK_ENABLE_POKEMON)
    setError(scope_ == Scope::PokemonOnly ? tr(STR_POKEMON_TRANSFER_NO_SAVE) : tr(STR_NEARBY_SYNC_NOTHING));
#else
    setError(tr(STR_NEARBY_SYNC_NOTHING));
#endif
    return false;
  }
  if (!source_.open(localMac_)) {
    setError(tr(STR_NEARBY_TRANSFER_SOURCE_FAILED));
    return false;
  }
  offer_.totalBytes = source_.totalBytes();
  offer_.chunkBytes = nearby::V2_CHUNK_BYTES;
  offer_.flags = (hasPokemon_ ? ns::OFFER_HAS_POKEMON : 0) | (hasStats_ ? ns::OFFER_HAS_STATS : 0) |
                 (hasBook_ ? ns::OFFER_HAS_BOOK : 0);
  const char* name = SETTINGS.getEffectiveDeviceName();
  copyText(offer_.senderName, name, strlen(name));
  return true;
}

// ---- Radio / protocol ----------------------------------------------------

bool NearbySyncActivity::startRadio() {
  if (transport_.started()) return true;
  radioUsed_ = true;
  if (!transport_.begin(ESPNOW_CHANNEL)) {
    setError(tr(STR_NEARBY_TRANSFER_RADIO_FAILED));
    return false;
  }
  return true;
}

void NearbySyncActivity::startListening() {
#if defined(CROSSINK_ENABLE_POKEMON)
  // A Pokemon save committed earlier but never installed must finish first
  // (a new one cannot be staged over it).
  if (!st::applyPendingSaveTransfer()) {
    setError(tr(STR_POKEMON_TRANSFER_INSTALL_PENDING));
    return;
  }
  receiverHasPokemonSave_ = st::hasLocalSave();
#endif
  if (!startRadio()) return;
  session_.reset();
  peerMac_ = {};
  offer_ = ns::Offer{};
  retryCount_ = 0;
  setState(State::Listening);
}

void NearbySyncActivity::startDiscovery() {
  offer_.flags = static_cast<uint8_t>((offer_.flags & ~ns::OFFER_MOVE_POKEMON) | (moveSave_ ? ns::OFFER_MOVE_POKEMON : 0));
  if (!startRadio()) return;
#if defined(ARDUINO_ARCH_ESP32) && !defined(SIMULATOR)
  sessionId_ = esp_random();
#else
  sessionId_ = static_cast<uint32_t>(millis()) ^ 0x4e53594eu;
#endif
  if (sessionId_ == 0) sessionId_ = 1;
  peerCount_ = 0;
  setState(State::Discovering);
  sendDiscovery();
}

bool NearbySyncActivity::sendPacket(const nearby::PacketType type, const uint8_t* destination, const uint32_t sequence,
                                    const void* payload, const uint16_t payloadLength) {
  size_t length = 0;
  if (!nearby::encodePacket(packetBuffer_.data(), packetBuffer_.size(), type, sessionId_, sequence, payload,
                            payloadLength, length)) {
    return false;
  }
  return transport_.send(destination, packetBuffer_.data(), length);
}

bool NearbySyncActivity::sendDiscovery() {
  lastActionMs_ = millis();
  return sendPacket(nearby::PacketType::Discover, BROADCAST_MAC, 0, ns::OFFER_TAG.data(),
                    static_cast<uint16_t>(ns::OFFER_TAG.size()));
}

bool NearbySyncActivity::sendAdvertisement(const uint8_t* destination) {
  const char* name = SETTINGS.getEffectiveDeviceName();
  const size_t nameLength = std::min<size_t>(strlen(name), ns::NAME_BYTES - 1);
  std::array<uint8_t, 4 + ns::NAME_BYTES> payload{};
  memcpy(payload.data(), ns::OFFER_TAG.data(), 4);
  memcpy(payload.data() + 4, name, nameLength);
  return sendPacket(nearby::PacketType::Advertise, destination, 0, payload.data(),
                    static_cast<uint16_t>(4 + nameLength));
}

void NearbySyncActivity::selectPeer() {
  if (peerCount_ == 0 || selectedIndex_ < 0 || selectedIndex_ >= peerCount_) return;
  selectedPeer_ = selectedIndex_;
  peerMac_ = peers_[selectedPeer_].mac;
  retryCount_ = 0;
  setState(State::WaitingForApproval);
  sendOffer();
}

bool NearbySyncActivity::sendOffer() {
  std::array<uint8_t, ns::OFFER_MAX_BYTES> payload{};
  size_t length = 0;
  if (!ns::encodeOffer(offer_, payload.data(), payload.size(), length)) return false;
  lastActionMs_ = millis();
  return sendPacket(nearby::PacketType::Offer, peerMac_.data(), 0, payload.data(), static_cast<uint16_t>(length));
}

void NearbySyncActivity::processPackets() {
  while (transport_.poll(eventBuffer_)) {
    nearby::PacketView packet;
    if (!nearby::decodePacket(eventBuffer_.data.data(), eventBuffer_.length, packet)) continue;
    if (role_ == Role::Send) {
      handleSenderPacket(eventBuffer_, packet);
    } else {
      handleReceiverPacket(eventBuffer_, packet);
    }
  }
}

void NearbySyncActivity::handleSenderPacket(const nearby::EspNowTransport::Event& event,
                                            const nearby::PacketView& packet) {
  if (packet.sessionId != sessionId_) return;
  if (packet.type == nearby::PacketType::Advertise && (state_ == State::Discovering || state_ == State::DeviceList)) {
    // Every nearby listener answers a Discover; only a tagged advertisement
    // comes from a sync receiver.
    if (!ns::hasOfferTag(packet.payload, packet.payloadLength) || packet.payloadLength < 5 ||
        packet.payloadLength > 4 + ns::NAME_BYTES - 1) {
      return;
    }
    for (uint8_t i = 0; i < peerCount_; ++i) {
      if (sameMac(peers_[i].mac, event.sourceMac.data())) return;
    }
    if (peerCount_ >= MAX_PEERS) return;
    Peer& peer = peers_[peerCount_++];
    peer.mac = event.sourceMac;
    copyText(peer.name, reinterpret_cast<const char*>(packet.payload + 4), packet.payloadLength - 4U);
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
    // Rewind (and re-checksum: the Pokemon section streams from SD files).
    if (!source_.open(localMac_) || source_.totalBytes() != offer_.totalBytes) {
      setError(tr(STR_NEARBY_TRANSFER_SOURCE_FAILED));
      return;
    }
    session_.begin(nearby::ReliableTransferSession::Role::Sender, sessionId_, offer_.totalBytes, negotiatedChunkBytes_);
    retryCount_ = 0;
    setState(State::Sending);
    sendNextChunk();
    return;
  }
  if (packet.type == nearby::PacketType::Reject && state_ == State::WaitingForApproval) {
    const uint8_t reason = packet.payloadLength == 1 ? packet.payload[0] : REJECT_USER;
#if defined(CROSSINK_ENABLE_POKEMON)
    if (reason == REJECT_VERSION) {
      setError(tr(STR_POKEMON_TRANSFER_INCOMPATIBLE));
      return;
    }
#endif
    setError(reason == REJECT_STORAGE ? tr(STR_NEARBY_TRANSFER_NO_SPACE) : tr(STR_NEARBY_TRANSFER_REJECTED));
    return;
  }
  if (packet.type == nearby::PacketType::Ack && state_ == State::Sending && packet.payloadLength == 4) {
    if (!session_.acceptAcknowledgement(nearby::readU32(packet.payload))) return;
    session_.advanceSentBytes(pendingChunkLength_);
    pendingChunkLength_ = 0;
    retryCount_ = 0;
    maybeRefreshProgress();
    if (session_.transferredBytes() == session_.totalBytes()) {
      sendComplete();
    } else {
      sendNextChunk();
    }
    return;
  }
  if (packet.type == nearby::PacketType::Result && state_ == State::Sending) {
    ns::Result result;
    if (!ns::decodeResult(packet.payload, packet.payloadLength, result)) return;
    if (result.status == ns::RESULT_OK) {
      finishSend(result);
    } else {
      setError(tr(STR_NEARBY_TRANSFER_VERIFY_FAILED));
    }
    return;
  }
  if (packet.type == nearby::PacketType::Cancel) setError(tr(STR_NEARBY_TRANSFER_CANCELLED));
}

void NearbySyncActivity::handleReceiverPacket(const nearby::EspNowTransport::Event& event,
                                              const nearby::PacketView& packet) {
  if (packet.type == nearby::PacketType::Discover && state_ == State::Listening) {
    if (!ns::hasOfferTag(packet.payload, packet.payloadLength)) return;
    sessionId_ = packet.sessionId;
    sendAdvertisement(event.sourceMac.data());
    return;
  }
  if (packet.type == nearby::PacketType::Offer && state_ == State::Listening) {
    ns::Offer offer;
    if (!ns::decodeOffer(packet.payload, packet.payloadLength, offer)) return;
    sessionId_ = packet.sessionId;
    peerMac_ = event.sourceMac;
    offer_ = offer;
    negotiatedChunkBytes_ = std::clamp<uint16_t>(offer.chunkBytes, nearby::COMPAT_CHUNK_BYTES, nearby::V2_CHUNK_BYTES);
#if defined(CROSSINK_ENABLE_POKEMON)
    const bool pokemonUnusable = offer.has(ns::OFFER_HAS_POKEMON) &&
                                 !pokemon::saveFormatCompatible(versionsFrom(offer.pokemon),
                                                                pokemon::currentSaveFormatVersions());
    const char* refusal = tr(STR_POKEMON_TRANSFER_INCOMPATIBLE);
#else
    // No Pokemon game here: the save would be dropped, which is only
    // acceptable if the sender keeps its own copy.
    const bool pokemonUnusable = offer.has(ns::OFFER_MOVE_POKEMON);
    const char* refusal = tr(STR_NEARBY_TRANSFER_FAILED);
#endif
    if (pokemonUnusable) {
      const uint8_t reason = REJECT_VERSION;
      sendPacket(nearby::PacketType::Reject, peerMac_.data(), 0, &reason, 1);
      setError(refusal);
      return;
    }
    setState(State::OfferPrompt);
    return;
  }
  if (packet.sessionId != sessionId_ || !sameMac(peerMac_, event.sourceMac.data())) return;
  if (packet.type == nearby::PacketType::Offer && state_ == State::Receiving) {
    uint8_t payload[2];  // our Accept was lost: repeat it
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
      const uint8_t failed = ns::RESULT_FAILED;
      sendPacket(nearby::PacketType::Result, peerMac_.data(), 0, &failed, 1);
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
      // The sender missed our answer: repeat it.
      sendPacket(nearby::PacketType::Result, peerMac_.data(), 0, resultBuffer_.data(),
                 static_cast<uint16_t>(resultLength_));
      return;
    }
    if (state_ != State::Receiving) return;
    if (!finishReceived(nearby::readU64(packet.payload), nearby::readU32(packet.payload + 8))) {
      const uint8_t failed = ns::RESULT_FAILED;
      sendPacket(nearby::PacketType::Result, peerMac_.data(), 0, &failed, 1);
      return;
    }
    buildResult(true);
    sendPacket(nearby::PacketType::Result, peerMac_.data(), 0, resultBuffer_.data(),
               static_cast<uint16_t>(resultLength_));
#if defined(CROSSINK_ENABLE_POKEMON)
    if (pokemonOutcome_ == Outcome::Applied) {
      setState(State::Installing);
      if (requestUpdateAndWait() != RequestUpdateResult::Rendered) requestUpdate(true);
      // Already committed: anything left over finishes on the next boot.
      installPending_ = !st::applyPendingSaveTransfer();
    }
#endif
    setState(State::Success);
    return;
  }
  if (packet.type == nearby::PacketType::Cancel && (state_ == State::Receiving || state_ == State::OfferPrompt)) {
    setError(tr(STR_NEARBY_TRANSFER_CANCELLED));
  }
}

void NearbySyncActivity::acceptOffer() {
  uint64_t total = 0;
  uint64_t used = 0;
#ifndef SIMULATOR
  total = Storage.totalBytes();
  used = Storage.usedBytes();
#endif
  // The container, the Pokemon bundle extracted from it, and the installed save.
  if (total > 0 && used <= total && 3ULL * offer_.totalBytes > total - used) {
    const uint8_t reason = REJECT_STORAGE;
    sendPacket(nearby::PacketType::Reject, peerMac_.data(), 0, &reason, 1);
    setError(tr(STR_NEARBY_TRANSFER_NO_SPACE));
    return;
  }
  ns::discardReceived();
  receiveFile_ = Storage.open(ns::RECEIVE_PATH, O_WRONLY | O_CREAT | O_TRUNC);
  if (!receiveFile_) {
    setError(tr(STR_NEARBY_TRANSFER_WRITE_FAILED));
    return;
  }
  session_.begin(nearby::ReliableTransferSession::Role::Receiver, sessionId_, offer_.totalBytes, negotiatedChunkBytes_);
  retryCount_ = 0;
  setState(State::Receiving);
  // Draw the progress screen before data arrives: a full e-ink refresh in the
  // middle of the transfer would stall the loop long enough for retries.
  if (requestUpdateAndWait() != RequestUpdateResult::Rendered) requestUpdate(true);
  uint8_t payload[2];
  nearby::writeU16(payload, negotiatedChunkBytes_);
  if (!sendPacket(nearby::PacketType::Accept, peerMac_.data(), 0, payload, sizeof(payload))) {
    setError(tr(STR_NEARBY_TRANSFER_RADIO_FAILED));
    return;
  }
  lastActionMs_ = millis();
}

void NearbySyncActivity::rejectOffer() {
  const uint8_t reason = REJECT_USER;
  sendPacket(nearby::PacketType::Reject, peerMac_.data(), 0, &reason, 1);
  startListening();
}

bool NearbySyncActivity::sendNextChunk() {
  pendingChunkLength_ = 0;
  // Fill a whole chunk: the container source may return short reads at
  // section boundaries.
  size_t filled = 0;
  while (filled < negotiatedChunkBytes_) {
    const int count = source_.read(chunkBuffer_.data() + filled, negotiatedChunkBytes_ - filled);
    if (count < 0) {
      setError(tr(STR_NEARBY_TRANSFER_SOURCE_FAILED));
      return false;
    }
    if (count == 0) break;
    filled += static_cast<size_t>(count);
  }
  if (filled == 0) {
    setError(tr(STR_NEARBY_TRANSFER_SOURCE_FAILED));
    return false;
  }
  pendingChunkLength_ = filled;
  session_.includeBytes(chunkBuffer_.data(), pendingChunkLength_);
  retryCount_ = 0;
  return resendPending();
}

bool NearbySyncActivity::resendPending() {
  lastActionMs_ = millis();
  return sendPacket(nearby::PacketType::Data, peerMac_.data(), session_.nextSequence(), chunkBuffer_.data(),
                    static_cast<uint16_t>(pendingChunkLength_));
}

bool NearbySyncActivity::sendAck() {
  uint8_t payload[4];
  nearby::writeU32(payload, session_.nextSequence());
  lastActionMs_ = millis();
  return sendPacket(nearby::PacketType::Ack, peerMac_.data(), 0, payload, sizeof(payload));
}

bool NearbySyncActivity::sendComplete() {
  uint8_t payload[12];
  nearby::writeU64(payload, session_.totalBytes());
  nearby::writeU32(payload + 8, session_.crc32());
  lastActionMs_ = millis();
  return sendPacket(nearby::PacketType::Complete, peerMac_.data(), 0, payload, sizeof(payload));
}

// ---- Receiver: applying what arrived --------------------------------------

// Transport check (byte count + whole-stream CRC), then the container's own
// per-section CRCs, then each section. The Pokemon save is staged and
// committed BEFORE the sender hears "OK", so a sender moving its save never
// deletes it before this reader is guaranteed to end up with it.
bool NearbySyncActivity::finishReceived(const uint64_t expectedBytes, const uint32_t expectedCrc) {
  const bool synced = receiveFile_.sync();
  const bool closed = receiveFile_.close();
  if (expectedBytes != session_.totalBytes() || expectedBytes != session_.transferredBytes() ||
      expectedCrc != session_.crc32()) {
    ns::discardReceived();
    setError(tr(STR_NEARBY_TRANSFER_VERIFY_FAILED));
    return false;
  }
  if (!synced || !closed) {
    ns::discardReceived();
    setError(tr(STR_NEARBY_TRANSFER_WRITE_FAILED));
    return false;
  }
  const bool applied = applyReceivedContainer();
  ns::discardReceived();
  return applied;
}

bool NearbySyncActivity::applyReceivedContainer() {
  ns::ContainerHeader header;
  const ns::VerifyResult verified = ns::verifyReceived(header);
  if (verified != ns::VerifyResult::Ok) {
    setError(tr(STR_NEARBY_TRANSFER_VERIFY_FAILED));
    return false;
  }
  if (header.find(ns::SectionType::PokemonSave) >= 0 && !stagePokemonSection(header)) return false;
  applyStatsSection(header);
  applyBookSection(header);
  return true;
}

bool NearbySyncActivity::stagePokemonSection(const ns::ContainerHeader& header) {
#if defined(CROSSINK_ENABLE_POKEMON)
  const size_t index = static_cast<size_t>(header.find(ns::SectionType::PokemonSave));
  pokemon::SaveBundleHeader bundle;
  if (!ns::extractSection(header, index, st::STAGING_PATH)) {
    setError(tr(STR_NEARBY_TRANSFER_WRITE_FAILED));
    return false;
  }
  const st::VerifyResult verified = st::verifyStagedBundle(bundle);
  if (verified != st::VerifyResult::Ok || !st::commitStagedBundle()) {
    st::discardStagedBundle();
    setError(verified == st::VerifyResult::UnsupportedVersion ? tr(STR_POKEMON_TRANSFER_INCOMPATIBLE)
                                                              : tr(STR_NEARBY_TRANSFER_VERIFY_FAILED));
    pokemonOutcome_ = Outcome::Failed;
    return false;
  }
  pokemonOutcome_ = Outcome::Applied;
  return true;
#else
  (void)header;
  return true;  // no Pokemon game on this reader: the section is ignored
#endif
}

void NearbySyncActivity::applyStatsSection(const ns::ContainerHeader& header) {
  const int index = header.find(ns::SectionType::ReadingStats);
  if (index < 0) return;
  std::array<uint8_t, ns::MAX_STATS_BYTES> stats{};
  size_t length = 0;
  const bool ok = ns::readSection(header, static_cast<size_t>(index), stats.data(), stats.size(), length) &&
                  header.senderMac != localMac_ && ns::storePeerStats(header.senderMac, stats.data(), length);
  statsOutcome_ = ok ? Outcome::Applied : Outcome::Failed;
}

void NearbySyncActivity::applyBookSection(const ns::ContainerHeader& header) {
  const int index = header.find(ns::SectionType::BookPosition);
  if (index < 0) return;
  std::array<uint8_t, ns::MAX_BOOK_RECORD_BYTES> bytes{};
  size_t length = 0;
  ns::BookPositionRecord record;
  if (!ns::readSection(header, static_cast<size_t>(index), bytes.data(), bytes.size(), length) ||
      !ns::decodeBookPosition(bytes.data(), length, record)) {
    bookOutcome_ = Outcome::Failed;
    return;
  }
  book_ = record;
  const std::string path = locateBook(record.path.data());
  if (path.empty()) {
    bookOutcome_ = Outcome::BookMissing;
    return;
  }
  const std::string cachePath = Epub::cachePathForFilePath(path, CACHE_ROOT);
  if (!Storage.ensureDirectoryExists(cachePath.c_str()) ||
      !ns::writeFileAtomic((cachePath + "/progress.bin").c_str(), record.progress.data(), record.progressLength)) {
    bookOutcome_ = Outcome::Failed;
    return;
  }
  if (record.percentBasisPoints != ns::UNKNOWN_PERCENT) {
    RecentBookProgress::saveCachedEpubPercent(cachePath, static_cast<float>(record.percentBasisPoints) / 100.0f);
  }
  // Becomes this reader's most recent book, like opening it would.
  const RecentBook existing = RECENT_BOOKS.getDataFromBook(path);
  RECENT_BOOKS.addOrUpdateBook(path, existing.title.empty() ? std::string(record.title.data()) : existing.title,
                               existing.author, existing.coverBmpPath, existing.coverState);
  receivedBookPath_ = path;
  bookOutcome_ = Outcome::Applied;
}

// OK + this reader's MAC + its own stats, so the sender stores them too.
void NearbySyncActivity::buildResult(const bool ok) {
  ns::Result result;
  result.status = ok ? ns::RESULT_OK : ns::RESULT_FAILED;
  result.deviceMac = localMac_;
  size_t statsLength = 0;
  if (ok && offer_.has(ns::OFFER_HAS_STATS) && localMac_ != std::array<uint8_t, ns::MAC_BYTES>{} &&
      readLocalStats(result.stats.data(), statsLength)) {
    result.statsLength = static_cast<uint8_t>(statsLength);
  }
  if (!ns::encodeResult(result, resultBuffer_.data(), resultBuffer_.size(), resultLength_)) {
    resultBuffer_[0] = result.status;
    resultLength_ = 1;
  }
}

void NearbySyncActivity::finishSend(const ns::Result& result) {
  // Stats travel both ways: the receiver's come back with its answer.
  statsOutcome_ = Outcome::None;
  if (result.statsLength > 0) {
    statsOutcome_ = ns::storePeerStats(result.deviceMac, result.stats.data(), result.statsLength) ? Outcome::Applied
                                                                                                  : Outcome::Failed;
  }
#if defined(CROSSINK_ENABLE_POKEMON)
  if (moveSave_ && hasPokemon_) {
    pokemonStream_.reset();  // close the save files before moving them
    if (!st::moveLocalSaveToBackup()) {
      setError(tr(STR_POKEMON_TRANSFER_MOVE_FAILED));
      return;
    }
  }
#endif
  setState(State::Success);
}

void NearbySyncActivity::cancelTransfer() {
  if (transport_.started() && peerMac_ != std::array<uint8_t, nearby::MAC_BYTES>{} &&
      (state_ == State::Sending || state_ == State::Receiving || state_ == State::WaitingForApproval ||
       state_ == State::OfferPrompt)) {
    sendPacket(nearby::PacketType::Cancel, peerMac_.data());
  }
  if (receiveFile_) {
    receiveFile_.close();
    ns::discardReceived();
  }
}

void NearbySyncActivity::setState(const State state) {
  state_ = state;
  selectedIndex_ = 0;
  uiReady_ = false;
  app_.clearTapFlash();
  lastUiMs_ = millis();
  requestUpdate();
}

void NearbySyncActivity::setError(const char* message) {
  errorMessage_ = message ? message : tr(STR_NEARBY_TRANSFER_FAILED);
  LOG_ERR(LOG_TAG, "%s", errorMessage_.c_str());
  cancelTransfer();
  setState(State::Error);
}

// The radio leaves Wi-Fi in a state the rest of the firmware does not expect,
// and a received save/position must be reloaded from SD anyway: restart.
void NearbySyncActivity::exitAfterRadio() {
  cancelTransfer();
  pokemonStream_.reset();
  transport_.end();
  if (radioUsed_) {
    silentRestart();
  } else {
    finish();
  }
}

void NearbySyncActivity::openReceivedBook() {
  if (receivedBookPath_.empty()) return;
  APP_STATE.openEpubPath = receivedBookPath_;
  APP_STATE.saveToFile();
  transport_.end();
  silentRestartToReader();
}

void NearbySyncActivity::updateTimers() {
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

void NearbySyncActivity::maybeRefreshProgress() {
  if (millis() - lastUiMs_ < UI_REFRESH_MS) return;
  lastUiMs_ = millis();
  requestUpdate();
}

// ---- UI -----------------------------------------------------------------

bool NearbySyncActivity::isMenuState() const {
  return state_ == State::ChooseRole || state_ == State::ChooseSendMode || state_ == State::DeviceList;
}

int NearbySyncActivity::menuItemCount() const {
  if (state_ == State::ChooseRole) return 2;
  if (state_ == State::ChooseSendMode) return hasPokemon_ ? 2 : 1;
  if (state_ == State::DeviceList) return peerCount_;
  return 0;
}

const char* NearbySyncActivity::menuLabel(const int index) const {
  if (state_ == State::ChooseRole) return index == 0 ? tr(STR_NEARBY_SYNC_SEND) : tr(STR_NEARBY_SYNC_RECEIVE);
  if (state_ == State::ChooseSendMode) {
    if (!hasPokemon_) return tr(STR_NEARBY_SYNC_START);
#if defined(CROSSINK_ENABLE_POKEMON)
    if (scope_ == Scope::PokemonOnly) return index == 0 ? tr(STR_POKEMON_TRANSFER_COPY) : tr(STR_POKEMON_TRANSFER_MOVE);
#endif
    return index == 0 ? tr(STR_NEARBY_SYNC_START_COPY) : tr(STR_NEARBY_SYNC_START_MOVE);
  }
  return index < peerCount_ ? peers_[index].name.data() : "";
}

int NearbySyncActivity::successActionCount() const {
  return role_ == Role::Receive && bookOutcome_ == Outcome::Applied ? 2 : 1;
}

void NearbySyncActivity::activateSelected() {
  switch (state_) {
    case State::ChooseRole:
      if (selectedIndex_ == 0) {
        role_ = Role::Send;
        if (prepareSend()) setState(State::ChooseSendMode);
      } else {
        role_ = Role::Receive;
        startListening();
      }
      break;
    case State::ChooseSendMode:
      moveSave_ = hasPokemon_ && selectedIndex_ == 1;
      startDiscovery();
      break;
    case State::DeviceList:
      selectPeer();
      break;
    case State::OfferPrompt:
      acceptOffer();
      break;
    case State::Success:
      if (successActionCount() == 2) {
        openReceivedBook();
      } else {
        exitAfterRadio();
      }
      break;
    case State::Error:
      exitAfterRadio();
      break;
    default:
      break;
  }
}

void NearbySyncActivity::menuScreen(UiApp::ScreenType& screen, void* user) {
  static_cast<NearbySyncActivity*>(user)->buildMenuScreen(screen);
}

void NearbySyncActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<NearbySyncActivity*>(user);
  if (!self->isMenuState() || event.value < 0 || event.value >= self->menuItemCount()) return;
  self->selectedIndex_ = event.value;
  self->app_.clearTapFlash();
  self->activateSelected();
}

void NearbySyncActivity::buildMenuScreen(UiApp::ScreenType& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // ChooseSendMode lists what will be sent above its buttons.
  const int16_t extraTop = state_ == State::ChooseSendMode ? 150 : 0;
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(metrics.topPadding + TouchHeaderBackButton::height(metrics, mappedInput) +
                                       metrics.verticalSpacing + extraTop),
                  0, static_cast<int16_t>(metrics.buttonHintsHeight + metrics.verticalSpacing), 0});

  std::array<fui::ListItem, MAX_PEERS> items{};
  const int count = menuItemCount();
  for (int i = 0; i < count; ++i) {
    items[i].label = menuLabel(i);
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

void NearbySyncActivity::updateNavigation() {
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

void NearbySyncActivity::loop() {
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
    const uint8_t actionCount = state_ == State::OfferPrompt ? 2 : (state_ == State::Success ? successActionCount() : 1);
    const int action = TouchActionButtons::indexAt(touchActionLayout(safeArea, actionCount), tx, ty);
    if (action == 0) {
      activateSelected();
    } else if (action == 1) {
      if (state_ == State::OfferPrompt) {
        rejectOffer();
      } else {
        exitAfterRadio();
      }
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

void NearbySyncActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const Rect header{0, metrics.topPadding, width, TouchHeaderBackButton::height(metrics, mappedInput)};
  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::draw(renderer, uiTarget_, header, title(), false);
  } else {
    GUI.drawHeader(renderer, header, title());
  }

  const Rect textArea{metrics.contentSidePadding, 0, width - metrics.contentSidePadding * 2, height};
  auto heading = [this, textArea](const char* text, const int y) {
    UITheme::drawCenteredWrappedTextAtCenter(renderer, textArea, UI_10_FONT_ID, y, text, 2, true, EpdFontFamily::BOLD);
  };
  auto note = [this, textArea](const char* text, const int y, const int maxLines = 2) {
    UITheme::drawCenteredWrappedTextAtCenter(renderer, textArea, SMALL_FONT_ID, y, text, maxLines);
  };
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int contentTop = metrics.topPadding + header.height + metrics.verticalSpacing;

  // One line per thing being synced - shared by the sender's confirmation and
  // the receiver's offer prompt.
  char line[160];
  auto pokemonLine = [&]() -> const char* {
#if defined(CROSSINK_ENABLE_POKEMON)
    const ns::PokemonOfferSummary& summary = offer_.pokemon;
    const pokemon::SpeciesData* species = pokemon::speciesData(summary.leaderSpeciesId);
    const char* leader = summary.leaderName[0] != '\0' ? summary.leaderName.data()
                         : species != nullptr          ? species->name
                                                       : "???";
    char detail[96];
    snprintf(detail, sizeof(detail), tr(STR_POKEMON_TRANSFER_SUMMARY), leader,
             static_cast<unsigned>(summary.leaderLevel), static_cast<unsigned>(summary.badgeCount),
             static_cast<unsigned>(summary.caughtCount));
    snprintf(line, sizeof(line), "%s: %s", tr(STR_NEARBY_SYNC_ITEM_POKEMON), detail);
#else
    snprintf(line, sizeof(line), "%s", tr(STR_NEARBY_SYNC_ITEM_POKEMON));
#endif
    return line;
  };
  auto bookLine = [&]() -> const char* {
    if (offer_.bookPercentBasisPoints != ns::UNKNOWN_PERCENT) {
      snprintf(line, sizeof(line), "%s: %s (%u%%)", tr(STR_NEARBY_SYNC_ITEM_BOOK), offer_.bookTitle.data(),
               static_cast<unsigned>((offer_.bookPercentBasisPoints + 50) / 100));
    } else {
      snprintf(line, sizeof(line), "%s: %s", tr(STR_NEARBY_SYNC_ITEM_BOOK), offer_.bookTitle.data());
    }
    return line;
  };

  uiReady_ = false;
  if (isMenuState()) {
    app_.render();
    uiReady_ = true;
    // Drawn after the list so its background never covers this summary.
    if (state_ == State::ChooseSendMode) {
      int y = contentTop + 16;
      heading(tr(STR_NEARBY_SYNC_WILL_SEND), y);
      y += lineHeight + 14;
      if (hasPokemon_) {
        note(pokemonLine(), y);
        y += 2 * lineHeight;
      }
      if (hasStats_) {
        note(tr(STR_NEARBY_SYNC_ITEM_STATS), y);
        y += lineHeight + 8;
      }
      if (hasBook_) note(bookLine(), y);
    }
  } else if (state_ == State::Listening) {
    heading(tr(STR_NEARBY_TRANSFER_LISTENING), height / 2);
  } else if (state_ == State::Discovering) {
    heading(tr(STR_NEARBY_TRANSFER_DISCOVERING), height / 2);
  } else if (state_ == State::WaitingForApproval) {
    heading(tr(STR_NEARBY_TRANSFER_WAITING_APPROVAL), height / 2 - 10);
    note(peers_[selectedPeer_].name.data(), height / 2 + lineHeight + 18);
  } else if (state_ == State::OfferPrompt) {
    int y = contentTop + 30;
    heading(tr(STR_NEARBY_SYNC_INCOMING), y);
    y += lineHeight + 8;
    note(offer_.senderName.data(), y);
    y += lineHeight + 24;
    if (offer_.has(ns::OFFER_HAS_POKEMON)) {
      note(pokemonLine(), y);
      y += 2 * lineHeight + 4;
#if defined(CROSSINK_ENABLE_POKEMON)
      if (receiverHasPokemonSave_) {
        note(tr(STR_POKEMON_TRANSFER_REPLACE_WARNING), y, 3);
        y += 3 * lineHeight;
      }
      if (offer_.has(ns::OFFER_MOVE_POKEMON)) {
        note(tr(STR_POKEMON_TRANSFER_MOVE_NOTE), y);
        y += lineHeight + 12;
      }
#endif
    }
    if (offer_.has(ns::OFFER_HAS_STATS)) {
      note(tr(STR_NEARBY_SYNC_ITEM_STATS), y);
      y += lineHeight + 12;
    }
    if (offer_.has(ns::OFFER_HAS_BOOK)) note(bookLine(), y);
  } else if (state_ == State::Sending || state_ == State::Receiving) {
    heading(state_ == State::Sending ? tr(STR_NEARBY_SYNC_SENDING) : tr(STR_NEARBY_SYNC_RECEIVING), height / 2 - 45);
    GUI.drawProgressBar(renderer,
                        Rect{metrics.contentSidePadding, height / 2 + 10, width - metrics.contentSidePadding * 2,
                             metrics.progressBarHeight},
                        static_cast<size_t>(session_.transferredBytes()), static_cast<size_t>(session_.totalBytes()));
  } else if (state_ == State::Installing) {
    heading(tr(STR_NEARBY_TRANSFER_VALIDATING), height / 2);
  } else if (state_ == State::Success) {
    int y = contentTop + 40;
    heading(tr(STR_NEARBY_SYNC_DONE), y);
    y += lineHeight + 24;
    if (role_ == Role::Receive) {
#if defined(CROSSINK_ENABLE_POKEMON)
      if (pokemonOutcome_ == Outcome::Applied) {
        note(installPending_ ? tr(STR_POKEMON_TRANSFER_INSTALL_PENDING) : tr(STR_NEARBY_SYNC_POKEMON_DONE), y);
        y += 2 * lineHeight;
      }
#endif
      if (statsOutcome_ != Outcome::None) {
        note(statsOutcome_ == Outcome::Applied ? tr(STR_NEARBY_SYNC_STATS_DONE) : tr(STR_NEARBY_SYNC_STATS_FAILED), y);
        y += 2 * lineHeight;
      }
      if (bookOutcome_ == Outcome::Applied) {
        note(tr(STR_NEARBY_SYNC_BOOK_DONE), y);
        y += lineHeight + 6;
        note(book_.title.data(), y);
      } else if (bookOutcome_ == Outcome::BookMissing) {
        snprintf(line, sizeof(line), tr(STR_NEARBY_SYNC_BOOK_MISSING), ns::baseName(book_.path.data()));
        note(line, y, 3);
      } else if (bookOutcome_ == Outcome::Failed) {
        note(tr(STR_NEARBY_SYNC_BOOK_FAILED), y);
      }
    } else {
      if (hasPokemon_) {
#if defined(CROSSINK_ENABLE_POKEMON)
        note(moveSave_ ? tr(STR_POKEMON_TRANSFER_MOVED) : tr(STR_POKEMON_TRANSFER_SENT), y, 3);
        y += 3 * lineHeight;
#endif
      }
      if (statsOutcome_ != Outcome::None) {
        note(statsOutcome_ == Outcome::Applied ? tr(STR_NEARBY_SYNC_STATS_DONE) : tr(STR_NEARBY_SYNC_STATS_FAILED), y);
        y += 2 * lineHeight;
      }
      if (hasBook_) note(bookLine(), y);
    }
  } else if (state_ == State::Error) {
    heading(tr(STR_NEARBY_TRANSFER_FAILED), height / 2);
    note(errorMessage_.c_str(), height / 2 + lineHeight + 10, 3);
  }

  const char* back = state_ == State::Installing ? "" : tr(STR_CANCEL);
  const char* confirm = "";
  if (isMenuState()) {
    confirm = tr(STR_SELECT);
  } else if (state_ == State::OfferPrompt) {
    confirm = tr(STR_ACCEPT);
  } else if (state_ == State::Success && successActionCount() == 2) {
    confirm = tr(STR_NEARBY_SYNC_OPEN_BOOK);
    back = tr(STR_OK);
  } else if (state_ == State::Success || state_ == State::Error) {
    back = "";
    confirm = tr(STR_OK);
  }
  const bool touchActions = mappedInput.hasTouch() &&
                            (state_ == State::OfferPrompt || state_ == State::Success || state_ == State::Error);
  if (touchActions) {
    const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const uint8_t actionCount = state_ == State::OfferPrompt ? 2 : (state_ == State::Success ? successActionCount() : 1);
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
