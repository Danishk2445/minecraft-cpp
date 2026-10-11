// Game.h -- owns the window and subsystems and runs the fixed-tick main loop.
#pragma once

#include <memory>

#include "app/Cli.h"
#include "core/JobSystem.h"
#include "platform/Window.h"

namespace mc {

class Game {
public:
    static constexpr double kTickSeconds = 0.05; // 20 ticks per second

    explicit Game(CliOptions options);
    ~Game();

    // Returns the process exit code.
    int run();

private:
    bool init();
    void shutdown();
    void handleGlobalKeys();
    void tick();
    void render(float partialTick);
    void afterFrame(); // harness: screenshot / exit when the run conditions are met

    CliOptions opts_;
    Window window_;
    std::unique_ptr<JobSystem> jobs_;

    long long frameCount_ = 0;
    long long tickCount_ = 0;
    bool harnessDone_ = false;
    bool screenshotRequested_ = false;
};

} // namespace mc
