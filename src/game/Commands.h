// Commands.h -- slash-command dispatcher (chat, --exec and test scripts).
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "core/Json.h"
#include "core/Math.h"
#include "game/WorldParams.h"

namespace mc {

class Level;

struct CommandSource {
    Level* level = nullptr;
    glm::dvec3 position{0.0};
    glm::vec2 rotation{0.0f}; // yaw, pitch
    std::string name = "Player";
    // Vanilla permission levels: 0 for anyone, 2+ for operators (a world with "Allow Commands").
    int permission = 4;
    int64_t seed = 0;
    std::function<void(const std::string&)> feedback;
    std::function<Difficulty()> getDifficulty;
    std::function<void(Difficulty)> setDifficulty;
    // Removes up to `max` of `item` (any item when null) from the player; the number
    // removed, or -1 for an unknown item.
    std::function<int(const std::string* item, int max)> clear;
    // Moves the executing entity (camera/player); rotation optional.
    std::function<void(const glm::dvec3&, std::optional<glm::vec2>)> teleport;
    // Changes the executing player's game mode; false for an unknown mode.
    std::function<bool(std::string_view)> setGameMode;
    // Gives the executing player items; false for an unknown item.
    std::function<bool(std::string_view item, int count)> give;
    // Harness: a failed assertion (/assertblock) is reported here.
    std::function<void(const std::string&)> assertionFailed;
    // Spawns an entity of `type` ("item", "experience_orb", ...) at a point with SNBT data;
    // false for a type that can't be summoned.
    std::function<bool(std::string_view type, const glm::dvec3& at, const Json& data, std::string& error)> summon;
    // /item replace block: puts `count` of item `id` into a container's slot; false when the
    // block has no such slot or the item is unknown.
    std::function<bool(const BlockPos&, int slot, std::string_view id, int count)> replaceBlockSlot;
    // /kill: the player dies (vanilla genericKill, past invulnerability and armor).
    std::function<void()> kill;
    // Experience: adds points or levels (negative removes); the query reads them.
    std::function<void(int amount, bool levels)> addExperience;
    std::function<int(bool levels)> experience;
};

class Commands {
public:
    using Args = std::span<const std::string>;
    using Handler = std::function<bool(CommandSource&, Args, std::string& error)>;

    // Command levels: anyone, or operators only (vanilla's level 2).
    static constexpr int kAnyone = 0, kOperator = 2;

    Commands(); // registers the built-in commands
    Commands(const Commands&) = delete; // /help reads the table it lives in
    Commands& operator=(const Commands&) = delete;

    void add(std::string name, std::string usage, Handler handler, int permission = kOperator);
    // `line` may start with '/'. Returns false and fills `error` on failure.
    bool execute(CommandSource& source, std::string_view line, std::string* error = nullptr);
    // The commands a source with `permission` may run.
    std::vector<std::string> names(int permission = 4) const;
    std::string usage(const std::string& name) const;

private:
    struct Command {
        std::string usage;
        Handler handler;
        int permission = kOperator;
    };
    void addBasics(); // help, seed, difficulty, list, say, me, clear, gamerule
    std::map<std::string, Command> commands_;
};

// "~", "~-2.5" or "12": relative to `base` when prefixed with '~'.
std::optional<double> parseCoordinate(std::string_view token, double base);
// Splits on spaces, keeping [...], {...} and quoted strings whole.
std::vector<std::string> tokenizeCommand(std::string_view line);
// Vanilla SNBT ({Item:{id:"minecraft:diamond",count:3b}}) as JSON: compounds, lists,
// quoted or bare strings, numbers (suffixes b s l f d dropped) and true/false.
std::optional<Json> parseSnbt(std::string_view text);

} // namespace mc
