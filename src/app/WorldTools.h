// WorldTools.h -- GL-free command-line tools for world generation: column
// diagnostics, biome maps with statistics, and a generation benchmark.
#pragma once

#include <optional>

#include "app/Cli.h"

namespace mc {

// Runs the world tool selected in `options`. Returns its exit code, or nullopt
// when no world tool was requested (the game then starts normally).
std::optional<int> runWorldTools(const CliOptions& options);

} // namespace mc
