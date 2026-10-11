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

namespace mc {

class Level;

struct CommandSource {
    Level* level = nullptr;
    glm::dvec3 position{0.0};
    glm::vec2 rotation{0.0f}; // yaw, pitch
    std::function<void(const std::string&)> feedback;
    // Moves the executing entity (camera/player); rotation optional.
    std::function<void(const glm::dvec3&, std::optional<glm::vec2>)> teleport;
    // Changes the executing player's game mode; false for an unknown mode.
    std::function<bool(std::string_view)> setGameMode;
    // Gives the executing player items; false for an unknown item.
    std::function<bool(std::string_view item, int count)> give;
    // Harness: a failed assertion (/assertblock) is reported here.
    std::function<void(const std::string&)> assertionFailed;
};

class Commands {
public:
    using Args = std::span<const std::string>;
    using Handler = std::function<bool(CommandSource&, Args, std::string& error)>;

    Commands(); // registers the built-in commands

    void add(std::string name, std::string usage, Handler handler);
    // `line` may start with '/'. Returns false and fills `error` on failure.
    bool execute(CommandSource& source, std::string_view line, std::string* error = nullptr);
    std::vector<std::string> names() const;
    std::string usage(const std::string& name) const;

private:
    struct Command {
        std::string usage;
        Handler handler;
    };
    std::map<std::string, Command> commands_;
};

// "~", "~-2.5" or "12": relative to `base` when prefixed with '~'.
std::optional<double> parseCoordinate(std::string_view token, double base);
std::vector<std::string> tokenizeCommand(std::string_view line);

} // namespace mc
