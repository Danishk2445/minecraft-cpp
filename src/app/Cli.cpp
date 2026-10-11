#include "app/Cli.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <functional>
#include <string_view>

namespace mc {
namespace {

bool parseInt(std::string_view s, int& out) {
    const auto* end = s.data() + s.size();
    auto [p, ec] = std::from_chars(s.data(), end, out);
    return ec == std::errc() && p == end;
}

bool parseSize(std::string_view s, int& w, int& h) {
    const size_t x = s.find('x');
    return x != std::string_view::npos && parseInt(s.substr(0, x), w) && parseInt(s.substr(x + 1), h) && w > 0 &&
           h > 0;
}

template <size_t N>
bool parseFloats(std::string_view s, std::array<double, N>& out) {
    for (size_t i = 0; i < N; ++i) {
        const size_t comma = s.find(',');
        const std::string_view part = s.substr(0, comma);
        const auto* end = part.data() + part.size();
        auto [p, ec] = std::from_chars(part.data(), end, out[i]);
        if (ec != std::errc() || p != end) return false;
        if (i + 1 < N) {
            if (comma == std::string_view::npos) return false;
            s.remove_prefix(comma + 1);
        } else if (comma != std::string_view::npos) {
            return false;
        }
    }
    return true;
}

bool parseBool(std::string_view s, bool& out) {
    if (s == "on" || s == "true" || s == "1") return out = true, true;
    if (s == "off" || s == "false" || s == "0") return out = false, true;
    return false;
}

struct Option {
    std::string_view name;
    std::string_view arg; // empty: flag without a value
    std::string_view help;
    std::function<bool(CliOptions&, std::string_view)> apply;
};

const std::vector<Option>& options() {
    static const std::vector<Option> opts = {
        {"--gamedir", "DIR", "game directory for saves, options and screenshots (default: run)",
         [](CliOptions& o, std::string_view v) { return o.gameDir = v, true; }},
        {"--assets", "DIR", "resource pack root (the folder containing assets/)",
         [](CliOptions& o, std::string_view v) { return o.assetsDir = v, true; }},
        {"--data", "DIR", "gameplay data directory", [](CliOptions& o, std::string_view v) { return o.dataDir = v, true; }},
        {"--shaders", "DIR", "GLSL directory", [](CliOptions& o, std::string_view v) { return o.shaderDir = v, true; }},
        {"--size", "WxH", "window size (default 1280x720)",
         [](CliOptions& o, std::string_view v) { return parseSize(v, o.width, o.height); }},
        {"--x11", "", "force the X11 backend (needed under xvfb-run)",
         [](CliOptions& o, std::string_view) { return o.forceX11 = true, true; }},
        {"--vsync", "on|off", "override the vsync option",
         [](CliOptions& o, std::string_view v) {
             bool b;
             if (!parseBool(v, b)) return false;
             o.vsync = b;
             return true;
         }},
        {"--gl-debug", "", "request a debug GL context and log driver messages",
         [](CliOptions& o, std::string_view) { return o.glDebug = true, true; }},
        {"--threads", "N", "worker threads (0 = run jobs inline on the main thread)",
         [](CliOptions& o, std::string_view v) { return parseInt(v, o.threads) && o.threads >= 0; }},
        {"--frames", "N", "harness: render at least N frames before --screenshot/--exit",
         [](CliOptions& o, std::string_view v) { return parseInt(v, o.frames) && o.frames >= 0; }},
        {"--ticks", "N", "harness: run at least N game ticks before --screenshot/--exit",
         [](CliOptions& o, std::string_view v) { return parseInt(v, o.ticks) && o.ticks >= 0; }},
        {"--screenshot", "FILE", "harness: save a PNG screenshot when the run conditions are met",
         [](CliOptions& o, std::string_view v) { return o.screenshot = v, true; }},
        {"--exit", "", "harness: quit once the run conditions are met",
         [](CliOptions& o, std::string_view) { return o.exitAfter = true, true; }},
        {"--world-type", "TYPE", "world generator: default, flat or debug",
         [](CliOptions& o, std::string_view v) {
             o.worldType = v;
             return v == "default" || v == "flat" || v == "debug";
         }},
        {"--seed", "N", "world seed",
         [](CliOptions& o, std::string_view v) {
             auto [p, ec] = std::from_chars(v.data(), v.data() + v.size(), o.seed);
             return ec == std::errc() && p == v.data() + v.size();
         }},
        {"--render-distance", "N", "render distance in chunks (2-32, default 12)",
         [](CliOptions& o, std::string_view v) { return parseInt(v, o.renderDistance) && o.renderDistance >= 2 && o.renderDistance <= 32; }},
        {"--exec", "CMDS", "commands to run once the spawn area is ready (';'-separated; repeatable)",
         [](CliOptions& o, std::string_view v) {
             size_t pos = 0;
             while (pos <= v.size()) {
                 const size_t semi = v.find(';', pos);
                 const std::string_view cmd = v.substr(pos, semi == std::string_view::npos ? std::string_view::npos : semi - pos);
                 if (!cmd.empty()) o.exec.emplace_back(cmd);
                 if (semi == std::string_view::npos) break;
                 pos = semi + 1;
             }
             return true;
         }},
        {"--wait-chunks", "", "harness: before --screenshot/--exit, wait until every visible chunk is meshed",
         [](CliOptions& o, std::string_view) { return o.waitChunks = true, true; }},
        {"--time", "TICKS", "initial time of day (0 sunrise, 6000 noon, 13000 night, 18000 midnight)",
         [](CliOptions& o, std::string_view v) {
             long long t = 0;
             auto [p, ec] = std::from_chars(v.data(), v.data() + v.size(), t);
             if (ec != std::errc() || p != v.data() + v.size()) return false;
             o.time = t;
             return true;
         }},
        {"--freeze-time", "", "stop the daylight cycle",
         [](CliOptions& o, std::string_view) { return o.freezeTime = true, true; }},
        {"--debug-overlay", "", "show the F3 debug screen",
         [](CliOptions& o, std::string_view) { return o.debugOverlay = true, true; }},
        {"--smooth-lighting", "on|off", "smooth lighting and ambient occlusion (default on)",
         [](CliOptions& o, std::string_view v) { return parseBool(v, o.smoothLighting); }},
        {"--gallery", "", "show the block gallery (every block state on a grid) instead of a world",
         [](CliOptions& o, std::string_view) { return o.gallery = true, true; }},
        {"--world", "NAME", "play the saved world <gamedir>/saves/NAME, creating it (with --seed and --world-type) if missing",
         [](CliOptions& o, std::string_view v) {
             if (v.empty() || v.find('/') != std::string_view::npos || v.find('\\') != std::string_view::npos || v == "..")
                 return false;
             o.world = v;
             return true;
         }},
        {"--gamemode", "MODE", "survival (default), creative, adventure or spectator",
         [](CliOptions& o, std::string_view v) {
             if (v != "survival" && v != "creative" && v != "adventure" && v != "spectator") return false;
             o.gameMode = v;
             return true;
         }},
        {"--pos", "X,Y,Z", "start the camera at this position (Y may be ~: 12 blocks above the generated terrain)",
         [](CliOptions& o, std::string_view v) {
             std::array<double, 3> a{};
             std::string text(v);
             const size_t c1 = text.find(','), c2 = c1 == std::string::npos ? c1 : text.find(',', c1 + 1);
             const bool aboveTerrain = c2 != std::string::npos && text.substr(c1 + 1, c2 - c1 - 1) == "~";
             if (aboveTerrain) text.replace(c1 + 1, 1, "nan");
             if (!parseFloats(text, a)) return false;
             o.pos = glm::dvec3(a[0], a[1], a[2]);
             return true;
         }},
        {"--hold", "ACTIONS", "harness: hold these controls down all run (forward, back, left, right, jump, sneak, sprint, attack, use; ','-separated)",
         [](CliOptions& o, std::string_view v) {
             static constexpr std::string_view kActions[] = {"forward", "back", "left", "right", "jump",
                                                             "sneak",   "sprint", "attack", "use"};
             for (size_t start = 0; start <= v.size();) {
                 const size_t comma = std::min(v.find(',', start), v.size());
                 const std::string_view a = v.substr(start, comma - start);
                 if (std::find(std::begin(kActions), std::end(kActions), a) == std::end(kActions)) return false;
                 o.hold.emplace_back(a);
                 start = comma + 1;
             }
             return true;
         }},
        {"--perspective", "VIEW", "first (default), back or front: first person or the F5 third-person views",
         [](CliOptions& o, std::string_view v) {
             if (v != "first" && v != "back" && v != "front") return false;
             o.perspective = v;
             return true;
         }},
        {"--look", "YAW,PITCH", "start the camera with this rotation (degrees)",
         [](CliOptions& o, std::string_view v) {
             std::array<double, 2> a{};
             if (!parseFloats(v, a)) return false;
             o.look = glm::vec2(float(a[0]), float(a[1]));
             return true;
         }},
        {"--dump-model-sheets", "DIR", "render every block state into tile sheets in DIR, then exit",
         [](CliOptions& o, std::string_view v) { return o.dumpModelSheets = v, true; }},
        {"--dump-column", "X,Z", "print terrain generator diagnostics for a block column, then exit",
         [](CliOptions& o, std::string_view v) {
             std::array<double, 2> a{};
             if (!parseFloats(v, a)) return false;
             o.dumpColumn = glm::ivec2(int(a[0]), int(a[1]));
             return true;
         }},
        {"--biome-map", "FILE", "write a top-down biome map PNG of the world around --pos (or 0,0) and print biome statistics",
         [](CliOptions& o, std::string_view v) { return o.biomeMap = v, true; }},
        {"--map-size", "N", "biome map size in blocks (default 4096)",
         [](CliOptions& o, std::string_view v) { return parseInt(v, o.mapSize) && o.mapSize > 0; }},
        {"--map-step", "N", "biome map blocks per pixel (default 4)",
         [](CliOptions& o, std::string_view v) { return parseInt(v, o.mapStep) && o.mapStep > 0; }},
        {"--slice", "FILE", "write a vertical cross-section PNG (x/y plane at --pos z, --map-size blocks wide), then exit",
         [](CliOptions& o, std::string_view v) { return o.slice = v, true; }},
        {"--check-data", "", "load blocks, tags, biomes and worldgen features, print every problem, then exit (1 if any)",
         [](CliOptions& o, std::string_view) { return o.checkData = true, true; }},
        {"--bench-gen", "N", "generate and light about N chunks around --pos (or 0,0), print chunks per second, then exit",
         [](CliOptions& o, std::string_view v) { return parseInt(v, o.benchGen) && o.benchGen > 0; }},
        {"--golden-tiles", "FILE", "with --dump-model-sheets: also save the tiles of the states listed in FILE",
         [](CliOptions& o, std::string_view v) { return o.goldenTiles = v, true; }},
        {"--log-level", "LEVEL", "debug, info, warn or error",
         [](CliOptions& o, std::string_view v) {
             o.logLevel = v;
             return v == "debug" || v == "info" || v == "warn" || v == "error";
         }},
        {"--help", "", "show this help", [](CliOptions& o, std::string_view) { return o.help = true, true; }},
    };
    return opts;
}

} // namespace

std::optional<CliOptions> parseCli(int argc, char** argv, std::string& error) {
    CliOptions out;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        std::string_view inlineValue;
        bool hasInline = false;
        if (const size_t eq = arg.find('='); arg.starts_with("--") && eq != std::string_view::npos) {
            inlineValue = arg.substr(eq + 1);
            arg = arg.substr(0, eq);
            hasInline = true;
        }
        const Option* opt = nullptr;
        for (const auto& o : options())
            if (o.name == arg) opt = &o;
        if (!opt) {
            error = std::format("unknown option '{}'", arg);
            return std::nullopt;
        }
        std::string_view value;
        if (!opt->arg.empty()) {
            if (hasInline) {
                value = inlineValue;
            } else if (i + 1 < argc) {
                value = argv[++i];
            } else {
                error = std::format("option {} needs a value ({})", arg, opt->arg);
                return std::nullopt;
            }
        }
        if (!opt->apply(out, value)) {
            error = std::format("bad value '{}' for {} (expected {})", value, arg, opt->arg);
            return std::nullopt;
        }
    }
    return out;
}

std::string cliUsage() {
    std::string s = "usage: minecraft [options]\n";
    for (const auto& o : options()) {
        const std::string head = o.arg.empty() ? std::string(o.name) : std::format("{} {}", o.name, o.arg);
        s += std::format("  {:<22} {}\n", head, o.help);
    }
    return s;
}

} // namespace mc
