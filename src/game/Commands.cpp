#include "game/Commands.h"

#include <charconv>
#include <cmath>
#include <format>
#include <limits>

#include "world/block/BlockRegistry.h"
#include "world/level/Level.h"

namespace mc {
namespace {

bool parseDouble(std::string_view s, double& out) {
    if (s.empty()) return false;
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc() && p == s.data() + s.size();
}

bool parseLong(std::string_view s, long long& out) {
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc() && p == s.data() + s.size();
}

std::optional<BlockPos> parseBlockPos(Commands::Args a, size_t i, const glm::dvec3& base) {
    if (a.size() < i + 3) return std::nullopt;
    const auto x = parseCoordinate(a[i], std::floor(base.x));
    const auto y = parseCoordinate(a[i + 1], std::floor(base.y));
    const auto z = parseCoordinate(a[i + 2], std::floor(base.z));
    if (!x || !y || !z) return std::nullopt;
    return BlockPos(int(std::floor(*x)), int(std::floor(*y)), int(std::floor(*z)));
}

} // namespace

std::optional<double> parseCoordinate(std::string_view token, double base) {
    double v = 0.0;
    if (token.starts_with('~')) {
        token.remove_prefix(1);
        if (token.empty()) return base;
        if (!parseDouble(token, v)) return std::nullopt;
        return base + v;
    }
    if (!parseDouble(token, v)) return std::nullopt;
    return v;
}

std::vector<std::string> tokenizeCommand(std::string_view line) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && line[i] == ' ') ++i;
        size_t j = i;
        int depth = 0; // spaces inside [...] stay in the token
        while (j < line.size() && (line[j] != ' ' || depth > 0)) {
            if (line[j] == '[') ++depth;
            if (line[j] == ']') --depth;
            ++j;
        }
        if (j > i) out.emplace_back(line.substr(i, j - i));
        i = j;
    }
    return out;
}

Commands::Commands() {
    addBasics();
    const Handler teleport = [](CommandSource& src, Args a, std::string& err) {
        if (a.size() != 3 && a.size() != 5) {
            err = "expected 3 or 5 arguments";
            return false;
        }
        const auto x = parseCoordinate(a[0], src.position.x), y = parseCoordinate(a[1], src.position.y),
                   z = parseCoordinate(a[2], src.position.z);
        if (!x || !y || !z) {
            err = "bad coordinates";
            return false;
        }
        std::optional<glm::vec2> rot;
        if (a.size() == 5) {
            const auto yaw = parseCoordinate(a[3], src.rotation.x), pitch = parseCoordinate(a[4], src.rotation.y);
            if (!yaw || !pitch) {
                err = "bad rotation";
                return false;
            }
            rot = glm::vec2(float(*yaw), float(*pitch));
        }
        if (src.teleport) src.teleport({*x, *y, *z}, rot);
        if (src.feedback) src.feedback(std::format("Teleported {} to {:.2f}, {:.2f}, {:.2f}", src.name, *x, *y, *z));
        return true;
    };
    add("tp", "/tp <x> <y> <z> [yaw pitch]", teleport);
    add("teleport", "/teleport <x> <y> <z> [yaw pitch]", teleport);

    add("gamemode", "/gamemode <survival|creative|adventure|spectator>", [](CommandSource& src, Args a, std::string& err) {
        if (a.size() != 1) {
            err = "expected a game mode";
            return false;
        }
        if (!src.setGameMode || !src.setGameMode(a[0])) {
            err = "unknown game mode " + a[0];
            return false;
        }
        if (src.feedback) {
            std::string mode = a[0];
            if (!mode.empty()) mode[0] = char(std::toupper(static_cast<unsigned char>(mode[0])));
            src.feedback("Set own game mode to " + mode + " Mode");
        }
        return true;
    });
    add("assertblock", "/assertblock <x> <y> <z> <block>", [](CommandSource& src, Args a, std::string& err) {
        if (a.size() != 4 || !src.level) {
            err = "expected x y z block";
            return false;
        }
        const auto x = parseCoordinate(a[0], src.position.x), y = parseCoordinate(a[1], src.position.y),
                   z = parseCoordinate(a[2], src.position.z);
        if (!x || !y || !z) {
            err = "bad coordinates";
            return false;
        }
        const BlockPos p(int(std::floor(*x)), int(std::floor(*y)), int(std::floor(*z)));
        const BlockRegistry& reg = src.level->registry();
        const auto expected = reg.parse(a[3]);
        const StateId actual = src.level->getBlock(p);
        // A bare block id matches any of its states.
        const bool ok = expected && (a[3].find('[') == std::string::npos ? reg.info(actual).block == reg.info(*expected).block
                                                                       : actual == *expected);
        if (!ok) {
            const std::string msg = std::format("block at {} {} {} is {}, expected {}", p.x, p.y, p.z, reg.describe(actual), a[3]);
            if (src.assertionFailed) src.assertionFailed(msg);
            err = msg;
            return false;
        }
        return true;
    });
    add("give", "/give <item> [count]", [](CommandSource& src, Args a, std::string& err) {
        if (a.empty() || a.size() > 2) {
            err = "expected an item and an optional count";
            return false;
        }
        int count = 1;
        if (a.size() == 2 && (!std::from_chars(a[1].data(), a[1].data() + a[1].size(), count).ptr || count <= 0)) {
            err = "bad count " + a[1];
            return false;
        }
        if (!src.give || !src.give(a[0], count)) {
            err = "unknown item " + a[0];
            return false;
        }
        if (src.feedback) src.feedback("Gave " + std::to_string(count) + " " + a[0]);
        return true;
    });
    add("setblock", "/setblock <x> <y> <z> <block>", [](CommandSource& src, Args a, std::string& err) {
        if (!src.level || a.size() != 4) {
            err = "expected 4 arguments";
            return false;
        }
        const auto pos = parseBlockPos(a, 0, src.position);
        if (!pos) {
            err = "bad coordinates";
            return false;
        }
        const auto state = src.level->registry().parse(a[3], &err);
        if (!state) return false;
        if (!src.level->setBlock(*pos, *state)) {
            err = "that position is not loaded";
            return false;
        }
        if (src.feedback) src.feedback(std::format("Changed the block at {}, {}, {}", pos->x, pos->y, pos->z));
        return true;
    });

    add("fill", "/fill <x1> <y1> <z1> <x2> <y2> <z2> <block>", [](CommandSource& src, Args a, std::string& err) {
        if (!src.level || a.size() != 7) {
            err = "expected 7 arguments";
            return false;
        }
        const auto p1 = parseBlockPos(a, 0, src.position), p2 = parseBlockPos(a, 3, src.position);
        if (!p1 || !p2) {
            err = "bad coordinates";
            return false;
        }
        const auto state = src.level->registry().parse(a[6], &err);
        if (!state) return false;
        const BlockPos lo(std::min(p1->x, p2->x), std::min(p1->y, p2->y), std::min(p1->z, p2->z));
        const BlockPos hi(std::max(p1->x, p2->x), std::max(p1->y, p2->y), std::max(p1->z, p2->z));
        const long long volume = (long long)(hi.x - lo.x + 1) * (hi.y - lo.y + 1) * (hi.z - lo.z + 1);
        if (volume > 32768) {
            err = std::format("too many blocks in the specified area (maximum 32768, specified {})", volume);
            return false;
        }
        int changed = 0;
        for (int y = lo.y; y <= hi.y; ++y)
            for (int z = lo.z; z <= hi.z; ++z)
                for (int x = lo.x; x <= hi.x; ++x)
                    if (src.level->getBlock({x, y, z}) != *state && src.level->setBlock({x, y, z}, *state)) ++changed;
        if (src.feedback) src.feedback(std::format("Successfully filled {} block(s)", changed));
        return true;
    });

    add("time", "/time (set|add) <ticks|day|noon|night|midnight> | /time query daytime",
        [](CommandSource& src, Args a, std::string& err) {
            if (!src.level || a.empty()) {
                err = "expected arguments";
                return false;
            }
            if (a[0] == "query") {
                if (src.feedback) src.feedback(std::format("The time is {}", src.level->dayTime() % Level::kDayLength));
                return true;
            }
            if (a.size() != 2 || (a[0] != "set" && a[0] != "add")) {
                err = "usage: /time set|add <value>";
                return false;
            }
            long long t = 0;
            if (a[1] == "day") t = 1000;
            else if (a[1] == "noon") t = 6000;
            else if (a[1] == "night") t = 13000;
            else if (a[1] == "midnight") t = 18000;
            else if (!parseLong(a[1], t)) {
                err = "bad time value";
                return false;
            }
            src.level->setDayTime(a[0] == "set" ? t : src.level->dayTime() + t);
            if (src.feedback) src.feedback(std::format("Set the time to {}", src.level->dayTime()));
            return true;
        });
}

void Commands::addBasics() {
    add("help", "/help [command]", [this](CommandSource& src, Args a, std::string& err) {
        if (a.size() > 1) {
            err = "expected at most one command";
            return false;
        }
        if (a.size() == 1) {
            auto it = commands_.find(a[0]);
            if (it == commands_.end() || src.permission < it->second.permission) {
                err = "unknown command " + a[0];
                return false;
            }
            if (src.feedback) src.feedback(it->second.usage);
            return true;
        }
        if (src.feedback)
            for (const auto& [name, c] : commands_)
                if (src.permission >= c.permission) src.feedback(c.usage);
        return true;
    }, kAnyone);

    // Single player shows the seed to anyone (vanilla asks for operators on servers only).
    add("seed", "/seed", [](CommandSource& src, Args a, std::string& err) {
        if (!a.empty()) {
            err = "expected no arguments";
            return false;
        }
        if (src.feedback) src.feedback(std::format("Seed: [{}]", src.seed));
        return true;
    }, kAnyone);

    add("difficulty", "/difficulty [peaceful|easy|normal|hard]", [](CommandSource& src, Args a, std::string& err) {
        if (!src.getDifficulty || !src.setDifficulty || a.size() > 1) {
            err = "expected an optional difficulty";
            return false;
        }
        auto display = [](Difficulty d) {
            std::string n(difficultyName(d));
            n[0] = char(std::toupper(static_cast<unsigned char>(n[0])));
            return n;
        };
        const Difficulty now = src.getDifficulty();
        if (a.empty()) {
            if (src.feedback) src.feedback("The difficulty is " + display(now));
            return true;
        }
        const auto d = difficultyFromName(a[0]);
        if (!d) {
            err = "unknown difficulty " + a[0];
            return false;
        }
        if (*d == now) {
            err = "The difficulty did not change; it is already set to " + display(now);
            return false;
        }
        src.setDifficulty(*d);
        if (src.feedback) src.feedback("The difficulty has been set to " + display(*d));
        return true;
    });

    add("list", "/list", [](CommandSource& src, Args, std::string&) {
        if (src.feedback) src.feedback("There are 1 of a max of 8 players online: " + src.name);
        return true;
    }, kAnyone);

    auto joined = [](Args a) {
        std::string text;
        for (const std::string& w : a) text += (text.empty() ? "" : " ") + w;
        return text;
    };
    add("say", "/say <message>", [joined](CommandSource& src, Args a, std::string& err) {
        if (a.empty()) {
            err = "expected a message";
            return false;
        }
        if (src.feedback) src.feedback("[" + src.name + "] " + joined(a));
        return true;
    });
    add("me", "/me <action>", [joined](CommandSource& src, Args a, std::string& err) {
        if (a.empty()) {
            err = "expected an action";
            return false;
        }
        if (src.feedback) src.feedback("* " + src.name + " " + joined(a));
        return true;
    }, kAnyone);

    add("clear", "/clear [item] [maxCount]", [](CommandSource& src, Args a, std::string& err) {
        if (!src.clear || a.size() > 2) {
            err = "expected an optional item and count";
            return false;
        }
        long long max = -1;
        if (a.size() == 2 && (!parseLong(a[1], max) || max < 0)) {
            err = "bad count " + a[1];
            return false;
        }
        const int removed = src.clear(a.empty() ? nullptr : &a[0], max < 0 ? std::numeric_limits<int>::max() : int(max));
        if (removed < 0) {
            err = "unknown item " + a[0];
            return false;
        }
        if (removed == 0) {
            err = "No items were found on player " + src.name;
            return false;
        }
        if (src.feedback) src.feedback(std::format("Removed {} item(s) from player {}", removed, src.name));
        return true;
    });

    // The rules that exist so far, with vanilla's names.
    add("gamerule", "/gamerule <doDaylightCycle> [true|false]", [](CommandSource& src, Args a, std::string& err) {
        if (!src.level || a.empty() || a.size() > 2) {
            err = "expected a rule and an optional value";
            return false;
        }
        if (a[0] != "doDaylightCycle") {
            err = "unknown game rule " + a[0];
            return false;
        }
        bool& rule = src.level->daylightCycle;
        if (a.size() == 1) {
            if (src.feedback) src.feedback(std::format("Gamerule {} is currently set to: {}", a[0], rule));
            return true;
        }
        if (a[1] != "true" && a[1] != "false") {
            err = "expected true or false";
            return false;
        }
        rule = a[1] == "true";
        if (src.feedback) src.feedback(std::format("Gamerule {} is now set to: {}", a[0], rule));
        return true;
    });
}

void Commands::add(std::string name, std::string usage, Handler handler, int permission) {
    commands_[std::move(name)] = {std::move(usage), std::move(handler), permission};
}

bool Commands::execute(CommandSource& source, std::string_view line, std::string* error) {
    while (!line.empty() && (line.front() == ' ' || line.front() == '/')) line.remove_prefix(1);
    const auto tokens = tokenizeCommand(line);
    std::string err;
    if (tokens.empty()) {
        if (error) *error = "empty command";
        return false;
    }
    auto it = commands_.find(tokens[0]);
    // Operator commands don't exist for others (vanilla: unknown command).
    if (it == commands_.end() || source.permission < it->second.permission) {
        if (error) *error = std::format("Unknown or incomplete command '{}'", tokens[0]);
        return false;
    }
    const bool ok = it->second.handler(source, Args(tokens).subspan(1), err);
    if (!ok && error) *error = std::format("{}: {} (usage: {})", tokens[0], err, it->second.usage);
    return ok;
}

std::vector<std::string> Commands::names(int permission) const {
    std::vector<std::string> out;
    for (const auto& [n, c] : commands_)
        if (permission >= c.permission) out.push_back(n);
    return out;
}

std::string Commands::usage(const std::string& name) const {
    auto it = commands_.find(name);
    return it == commands_.end() ? std::string() : it->second.usage;
}

} // namespace mc
