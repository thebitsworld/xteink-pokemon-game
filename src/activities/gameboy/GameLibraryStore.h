#pragma once

#if CROSSINK_GAMEBOY

#include <string>
#include <vector>

// Favourite and recently played Game Boy ROMs (/.crosspoint/gameboy_library.txt,
// one "F:<path>" or "R:<path>" per line, recents newest first). The library
// screen's filters read it; GameBoyActivity notes each game it starts.
namespace GameLibraryStore {

constexpr size_t MAX_FAVORITES = 64;
constexpr size_t MAX_RECENTS = 12;

struct Lists {
  std::vector<std::string> favorites;
  std::vector<std::string> recents;
};

Lists load();
bool save(const Lists& lists);
bool isFavorite(const Lists& lists, const std::string& path);
void toggleFavorite(Lists& lists, const std::string& path);
// Moves `path` to the front of the recents and saves.
void noteRecent(const std::string& path);
// The battery save (.sav), quick save (.state) and wake save (.resume) next to
// a ROM, as GameBoyActivity names them.
std::vector<std::string> savePathsFor(const std::string& romPath);
bool hasSave(const std::string& romPath);
// Deletes them; false when one could not be removed.
bool deleteSaves(const std::string& romPath);

}  // namespace GameLibraryStore

#endif
