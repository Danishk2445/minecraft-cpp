#include "game/Commands.h"

#include <charconv>
#include <cmath>
#include <format>

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
    add("tp", "/tp <x> <y> <z> [yaw pitch]", [](CommandSource& src, Args a, std::string& err) {
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
        if (src.feedback) src.feedback(std::format("Teleported to {:.2f}, {:.2f}, {:.2f}", *x, *y, *z));
        return true;
    });

    add("gamemode", "/gamemode <survival|creative|adventure|spectator>", [](CommandSource& src, Args a, std::string& err) {
        if (a.size() != 1) {
            err = "expected a game mode";
            return false;
        }
        if (!src.setGameMode || !src.setGameMode(a[0])) {
            err = "unknown game mode " + a[0];
            return false;
        }
        if (src.feedback) src.feedback("Set own game mode to " + a[0]);
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

void Commands::add(std::string name, std::string usage, Handler handler) {
    commands_[std::move(name)] = {std::move(usage), std::move(handler)};
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
    if (it == commands_.end()) {
        if (error) *error = std::format("unknown command '{}'", tokens[0]);
        return false;
    }
    const bool ok = it->second.handler(source, Args(tokens).subspan(1), err);
    if (!ok && error) *error = std::format("{}: {} (usage: {})", tokens[0], err, it->second.usage);
    return ok;
}

std::vector<std::string> Commands::names() const {
    std::vector<std::string> out;
    for (const auto& [n, c] : commands_) out.push_back(n);
    return out;
}

std::string Commands::usage(const std::string& name) const {
    auto it = commands_.find(name);
    return it == commands_.end() ? std::string() : it->second.usage;
}

} // namespace mc
