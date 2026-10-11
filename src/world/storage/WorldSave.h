// WorldSave.h -- world files besides chunks: <world>/level.json (seed, type,
// spawn, times) and <world>/playerdata/player.json.
#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "core/Json.h"

namespace mc {

class ItemRegistry;
class Player;

struct WorldInfo {
    std::string name;
    int64_t seed = 0;
    std::string type = "default";
    Json level; // Level::saveData()
};

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
