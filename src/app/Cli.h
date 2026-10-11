// Cli.h -- command-line options, including the scripted test harness.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

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

    std::string logLevel = "info";
    bool help = false;
};

// Returns nullopt and fills `error` for unknown or malformed options.
std::optional<CliOptions> parseCli(int argc, char** argv, std::string& error);
std::string cliUsage();

} // namespace mc
