// WorldSave.h -- world files besides chunks: <world>/level.json (seed, type,
// spawn, times) and <world>/playerdata/player.json.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/Json.h"

namespace mc {

class ItemRegistry;
class Player;

struct WorldInfo {
    std::string name;
    int64_t seed = 0;
    std::string type = "default";
    Json level; // Level::saveData()
    std::string gameMode = "survival";
    std::string difficulty = "normal";
    bool cheats = false;
    bool hardcore = false;
    int64_t lastPlayed = 0; // milliseconds since the epoch, set when written
};

// A world in the saves folder, for the world list.
struct WorldSummary {
    std::string folder;
    WorldInfo info;
    bool hasIcon = false; // icon.png
};
// Every world under `savesDir` (folders with a level.json), most recently played first.
std::vector<WorldSummary> listWorlds(const std::filesystem::path& savesDir);
// A folder name for a new world called `name` (vanilla FileUtil.findAvailableName):
// characters files can't hold become '_', and " (1)", " (2)", ... avoid existing folders.
std::string uniqueWorldFolder(const std::filesystem::path& savesDir, std::string_view name);
// The seed for text typed into the Create World screen: a number as is, other text by
// Java's String.hashCode, and a random one when empty.
int64_t seedFromString(std::string_view text, uint64_t randomSeed);
// Deletes a world's folder; false (with `error`) if it could not.
bool deleteWorld(const std::filesystem::path& savesDir, const std::string& folder, std::string* error = nullptr);

// Reads level.json; nullopt if the world does not exist or the file is unreadable.
std::optional<WorldInfo> readWorldInfo(const std::filesystem::path& worldDir, std::string* error = nullptr);
bool writeWorldInfo(const std::filesystem::path& worldDir, const WorldInfo& info, std::string* error = nullptr);

Json savePlayer(const Player& player);
// Applies saved position, look, game mode, flight and inventory.
void loadPlayer(Player& player, const Json& j, const ItemRegistry& items);
std::optional<Json> readPlayerFile(const std::filesystem::path& worldDir);
bool writePlayerFile(const std::filesystem::path& worldDir, const Json& j, std::string* error = nullptr);

// Writes text atomically (temp file, then rename).
bool writeFileAtomic(const std::filesystem::path& path, const std::string& text, std::string* error = nullptr);

} // namespace mc
