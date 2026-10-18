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
        {"--script", "FILE",
         "harness: play an action script (find, mine, craft, place, smelt, expect; see ActionScript.h) once the "
         "world is ready; --exit waits for it, and a failed step exits with 1",
         [](CliOptions& o, std::string_view v) { return o.script = v, !v.empty(); }},
        {"--tick-speed", "N", "harness: run game ticks N times faster than real time (1-100)",
         [](CliOptions& o, std::string_view v) { return parseInt(v, o.tickSpeed) && o.tickSpeed >= 1 && o.tickSpeed <= 100; }},
        {"--frames", "N", "harness: render at least N frames before --screenshot/--exit",
         [](CliOptions& o, std::string_view v) { return parseInt(v, o.frames) && o.frames >= 0; }},
        {"--ticks", "N", "harness: run at least N game ticks before --screenshot/--exit (with --exec, --wait-chunks or --screen: after the world is ready)",
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
        {"--render-distance", "N", "render distance in chunks (2-32; overrides the option)",
         [](CliOptions& o, std::string_view v) {
             int rd = 0;
             if (!parseInt(v, rd) || rd < 2 || rd > 32) return false;
             o.renderDistance = rd;
             return true;
         }},
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
        {"--wait-chunks", "", "harness: before --screenshot/--exit, wait until every visible chunk is meshed and drawn",
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
        {"--weather", "clear|rain|thunder", "start in this weather at full strength, and keep it",
         [](CliOptions& o, std::string_view v) {
             if (v != "clear" && v != "rain" && v != "thunder") return false;
             o.weather = std::string(v);
             return true;
         }},
        {"--gamerule", "NAME=VALUE[,...]", "set game rules as the world opens, before chunks generate (doMobSpawning=false)",
         [](CliOptions& o, std::string_view v) {
             while (!v.empty()) {
                 const size_t comma = v.find(',');
                 const std::string_view one = v.substr(0, comma);
                 const size_t eq = one.find('=');
                 if (eq == std::string_view::npos) return false;
                 o.gameRules.emplace_back(std::string(one.substr(0, eq)), std::string(one.substr(eq + 1)));
                 v = comma == std::string_view::npos ? std::string_view() : v.substr(comma + 1);
             }
             return true;
         }},
        {"--fov-effects", "SCALE", "harness: FOV effects 0..1 (sprinting, flying, bows); 0 by default for stable framing",
         [](CliOptions& o, std::string_view v) {
             std::array<double, 1> f{};
             if (!parseFloats(v, f) || f[0] < 0.0 || f[0] > 1.0) return false;
             o.fovEffects = float(f[0]);
             return true;
         }},
        {"--view-bobbing", "on|off", "harness: view bobbing while walking (off by default for stable framing)",
         [](CliOptions& o, std::string_view v) {
             bool b = false;
             if (!parseBool(v, b)) return false;
             o.viewBobbing = b;
             return true;
         }},
        {"--debug-overlay", "", "show the F3 debug screen",
         [](CliOptions& o, std::string_view) { return o.debugOverlay = true, true; }},
        {"--hide-gui", "", "hide the HUD and the hand (F1), for clean screenshots",
         [](CliOptions& o, std::string_view) { return o.hideGui = true, true; }},
        {"--smooth-lighting", "on|off", "smooth lighting and ambient occlusion (default on)",
         [](CliOptions& o, std::string_view v) {
             bool on = true;
             if (!parseBool(v, on)) return false;
             o.smoothLighting = on;
             return true;
         }},
        {"--cave-culling", "on|off", "draw only the sections the occlusion search reaches (default on; off to compare)",
         [](CliOptions& o, std::string_view v) { return parseBool(v, o.caveCulling); }},
        {"--graphics", "MODE", "fast, fancy or fabulous (overrides the option)",
         [](CliOptions& o, std::string_view v) {
             if (v == "fast") o.graphics = 0;
             else if (v == "fancy") o.graphics = 1;
             else if (v == "fabulous") o.graphics = 2;
             else return false;
             return true;
         }},
        {"--freeze-textures", "", "harness: keep animated textures (water, lava, fire...) on their first frame, for image diffs",
         [](CliOptions& o, std::string_view) { return o.freezeTextures = true, true; }},
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
        {"--screen", "NAME",
         "harness: open a screen at start, or in a world once the spawn area is ready (inventory, pause, options, "
         "video, controls, keybinds, chat, title, worlds, create)",
         [](CliOptions& o, std::string_view v) {
             static constexpr std::string_view kScreens[] = {"inventory", "pause", "options", "video",  "controls",
                                                             "keybinds",  "chat",  "title",   "worlds", "create"};
             if (std::find(std::begin(kScreens), std::end(kScreens), v) == std::end(kScreens)) return false;
             o.screen = v;
             if (v == "inventory" || v == "pause" || v == "chat") o.worldRequested = true;
             return true;
         }},
        {"--play", "NAME",
         "harness: start at the title and open the world <gamedir>/saves/NAME (created if missing) the way the world "
         "list does, behind the loading screen",
         [](CliOptions& o, std::string_view v) { return o.play = v, !v.empty(); }},
        {"--splash", "TEXT", "harness: show this splash on the title screen (empty: none) instead of a random one",
         [](CliOptions& o, std::string_view v) { return o.splash = std::string(v), true; }},
        {"--bake-panorama", "", "render the title screen panorama into <gamedir>/panorama, then exit",
         [](CliOptions& o, std::string_view) { return o.bakePanorama = true; }},
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
        {"--dump-item-icons", "DIR", "render every item's GUI icon into a sheet in DIR and report empty or missing-texture icons, then exit",
         [](CliOptions& o, std::string_view v) { return o.dumpItemIcons = v, true; }},
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
        {"--bench-fly", "SECONDS", "once the view is meshed, fly a fixed path east for SECONDS (spectator, no vsync or frame cap), print frame rate, lows and memory, then exit",
         [](CliOptions& o, std::string_view v) {
             std::array<double, 1> a{};
             if (!parseFloats(v, a) || !(a[0] > 0.0)) return false;
             o.benchFly = a[0];
             o.gameMode = "spectator";
             return true;
         }},
        {"--bench-out", "FILE", "with --bench-fly: also write the results to FILE as JSON",
         [](CliOptions& o, std::string_view v) { return o.benchOut = v, !v.empty(); }},
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
        // Options about a world open it directly instead of the title screen.
        static constexpr std::string_view kWorldOptions[] = {
            "--world", "--world-type", "--seed", "--pos", "--look", "--gamemode", "--exec", "--time",
            "--freeze-time", "--weather", "--perspective", "--hold", "--wait-chunks", "--debug-overlay", "--gamerule",
            "--bench-fly", "--script"};
        if (std::find(std::begin(kWorldOptions), std::end(kWorldOptions), arg) != std::end(kWorldOptions))
            out.worldRequested = true;
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
