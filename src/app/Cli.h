// Cli.h -- command-line options, including the scripted test harness.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace mc {

struct CliOptions {
    // Paths
    std::filesystem::path gameDir = "run";
    std::filesystem::path assetsDir = MC_DEFAULT_PACK_DIR;
    std::filesystem::path dataDir = MC_DEFAULT_DATA_DIR;
    std::filesystem::path shaderDir = MC_DEFAULT_SHADER_DIR;

    // Window and GL
    int width = 1280;
    int height = 720;
    bool forceX11 = false;
    std::optional<bool> vsync;
    bool glDebug = false;
    int threads = -1; // -1 = auto, 0 = run jobs inline on the main thread

    // Harness: once at least `frames` frames and `ticks` ticks have passed,
    // save `screenshot` (if set) and quit (if `exitAfter`).
    int frames = 0;
    int ticks = 0;
    std::string screenshot;
    bool exitAfter = false;

    // World
    std::string worldType = "default"; // default | flat | debug
    std::string gameMode = "survival"; // survival | creative | adventure | spectator
    std::string world;                 // save name under <gameDir>/saves; empty: an unsaved world
    long long seed = 0;
    std::optional<int> renderDistance; // overrides the option
    std::vector<std::string> exec; // commands run once the spawn area is ready
    std::vector<std::string> hold; // harness: controls held down the whole run (forward, attack, ...)
    bool waitChunks = false;       // harness: also wait until every visible chunk is meshed
    bool gallery = false;          // show the M1 block gallery instead of a world
    std::optional<long long> time; // --time: initial time of day (ticks)
    bool freezeTime = false;       // stop the daylight cycle (deterministic screenshots)
    std::string weather;           // clear, rain or thunder from the start (and no weather cycle)
    std::optional<float> fovEffects; // the FOV Effects scale (harness runs: 0 unless given)
    std::vector<std::pair<std::string, std::string>> gameRules; // set before any chunk generates
    bool debugOverlay = false;     // show the F3 screen from the start
    std::optional<bool> smoothLighting; // overrides the option

    // Camera / world placement
    std::optional<glm::dvec3> pos;  // --pos X,Y,Z
    std::optional<glm::vec2> look;  // --look YAW,PITCH (degrees)
    std::string perspective = "first"; // first | back | front (F5 views)
    std::string screen;                // harness: open this screen (in a world once it is ready)
    std::optional<std::string> splash; // harness: this title splash instead of a random one
    std::string play; // harness: open saves/<play> as the world list does (title, loading screen, game)
    bool worldRequested = false;       // some option names a world: open it instead of the title screen

    // Tools
    std::filesystem::path dumpModelSheets; // render every block state to sheets, then exit
    std::filesystem::path goldenTiles;     // states whose tiles are also saved individually
    std::filesystem::path dumpItemIcons;   // render every item's GUI icon into a sheet, then exit
    std::optional<glm::ivec2> dumpColumn;  // print worldgen diagnostics for a column, then exit
    std::filesystem::path biomeMap;        // write a top-down biome map PNG and statistics, then exit
    int mapSize = 4096;                    // biome map: width/height in blocks
    int mapStep = 4;                       // biome map: blocks per pixel
    int benchGen = 0;                      // generate and light about N chunks, report throughput, exit
    std::filesystem::path slice;           // write a vertical x/y cross-section PNG of the terrain, then exit
    bool checkData = false;                // load blocks, tags, biomes and features; report every problem; exit
    bool bakePanorama = false;             // render the title panorama into <gameDir>/panorama, then exit

    std::string logLevel = "info";
    bool help = false;
};

// Returns nullopt and fills `error` for unknown or malformed options.
std::optional<CliOptions> parseCli(int argc, char** argv, std::string& error);
std::string cliUsage();

} // namespace mc
