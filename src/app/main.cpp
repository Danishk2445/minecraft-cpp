#include <cstdio>

#include "app/Cli.h"
#include "app/Game.h"
#include "app/WorldTools.h"
#include "core/Log.h"

int main(int argc, char** argv) {
    std::string error;
    auto options = mc::parseCli(argc, argv, error);
    if (!options) {
        std::fprintf(stderr, "minecraft: %s\n\n%s", error.c_str(), mc::cliUsage().c_str());
        return 2;
    }
    if (options->help) {
        std::printf("%s", mc::cliUsage().c_str());
        return 0;
    }
    if (options->logLevel == "debug") mc::log::setLevel(mc::log::Level::Debug);
    if (options->logLevel == "warn") mc::log::setLevel(mc::log::Level::Warn);
    if (options->logLevel == "error") mc::log::setLevel(mc::log::Level::Error);

    if (auto code = mc::runWorldTools(*options)) return *code;

    mc::Game game(std::move(*options));
    return game.run();
}
