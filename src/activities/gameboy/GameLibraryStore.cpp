#include "GameLibraryStore.h"

#if CROSSINK_GAMEBOY

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>

namespace GameLibraryStore {
namespace {

constexpr char TAG[] = "GBLIB";
constexpr char PATH[] = "/.crosspoint/gameboy_library.txt";
constexpr char TMP_PATH[] = "/.crosspoint/gameboy_library.tmp";
constexpr size_t MAX_FILE_BYTES = 16 * 1024;

void removeFrom(std::vector<std::string>& list, const std::string& path) {
  list.erase(std::remove(list.begin(), list.end(), path), list.end());
}

}  // namespace

Lists load() {
  Lists lists;
  HalFile file;
  if (!Storage.exists(PATH) || !Storage.openFileForRead(TAG, PATH, file)) return lists;
  std::string text;
  char buffer[256];
  while (text.size() < MAX_FILE_BYTES) {
    const int got = file.read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
    if (got <= 0) break;
    text.append(buffer, static_cast<size_t>(got));
  }
  file.close();

  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    const std::string line = text.substr(start, end - start);
    start = end + 1;
    if (line.size() < 3 || line[1] != ':' || line[2] != '/') continue;
    const std::string path = line.substr(2);
    if (line[0] == 'F' && lists.favorites.size() < MAX_FAVORITES) {
      lists.favorites.push_back(path);
    } else if (line[0] == 'R' && lists.recents.size() < MAX_RECENTS) {
      lists.recents.push_back(path);
    }
  }
  return lists;
}

bool save(const Lists& lists) {
  std::string text;
  for (const auto& path : lists.favorites) text += "F:" + path + "\n";
  for (const auto& path : lists.recents) text += "R:" + path + "\n";
  (void)Storage.mkdir("/.crosspoint");
  HalFile file;
  if (!Storage.openFileForWrite(TAG, TMP_PATH, file)) {
    LOG_ERR(TAG, "Could not write %s", TMP_PATH);
    return false;
  }
  const bool written = file.write(reinterpret_cast<const uint8_t*>(text.data()), text.size()) == text.size();
  file.close();
  if (!written) {
    Storage.remove(TMP_PATH);
    return false;
  }
  Storage.remove(PATH);
  return Storage.rename(TMP_PATH, PATH);
}

bool isFavorite(const Lists& lists, const std::string& path) {
  return std::find(lists.favorites.begin(), lists.favorites.end(), path) != lists.favorites.end();
}

void toggleFavorite(Lists& lists, const std::string& path) {
  if (isFavorite(lists, path)) {
    removeFrom(lists.favorites, path);
  } else if (lists.favorites.size() < MAX_FAVORITES) {
    lists.favorites.push_back(path);
  }
  save(lists);
}

void noteRecent(const std::string& path) {
  Lists lists = load();
  if (!lists.recents.empty() && lists.recents.front() == path) return;
  removeFrom(lists.recents, path);
  lists.recents.insert(lists.recents.begin(), path);
  if (lists.recents.size() > MAX_RECENTS) lists.recents.resize(MAX_RECENTS);
  save(lists);
}

std::vector<std::string> savePathsFor(const std::string& romPath) {
  std::string base = romPath;
  const size_t slash = base.find_last_of('/');
  const size_t dot = base.find_last_of('.');
  if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) base.erase(dot);
  return {base + ".sav", base + ".sav.bak", base + ".state", base + ".resume"};
}

bool hasSave(const std::string& romPath) {
  for (const auto& path : savePathsFor(romPath)) {
    if (Storage.exists(path.c_str())) return true;
  }
  return false;
}

bool deleteSaves(const std::string& romPath) {
  bool ok = true;
  for (const auto& path : savePathsFor(romPath)) {
    if (Storage.exists(path.c_str()) && !Storage.remove(path.c_str())) {
      LOG_ERR(TAG, "Could not delete %s", path.c_str());
      ok = false;
    }
  }
  return ok;
}

}  // namespace GameLibraryStore

#endif
