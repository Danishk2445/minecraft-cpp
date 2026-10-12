// WorldParams.h -- how to open a world: a save folder (or none for a throwaway
// world), and for new worlds the settings from the Create World screen.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mc {

enum class Difficulty : uint8_t { Peaceful, Easy, Normal, Hard };
std::string_view difficultyName(Difficulty d);
std::optional<Difficulty> difficultyFromName(std::string_view name);

struct WorldParams {
    std::string folder;           // under saves/; empty: an unsaved world
    std::string name = "New World"; // shown in the world list
    std::string type = "default"; // default | flat | debug (new worlds)
    int64_t seed = 0;
    std::string gameMode = "survival";
    Difficulty difficulty = Difficulty::Normal;
    bool cheats = false; // "Allow Commands"
    bool hardcore = false; // survival locked to Hard, one life
    bool commandLine = false; // opened by the command line: the harness options apply (--pos, --time, ...)
};

} // namespace mc
