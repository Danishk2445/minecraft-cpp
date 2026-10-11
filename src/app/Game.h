// Game.h -- owns the window and subsystems and runs the fixed-tick main loop.
#pragma once

#include <memory>

#include "app/Cli.h"
#include "core/JobSystem.h"
#include "core/Timer.h"
#include "game/Commands.h"
#include "gfx/Camera.h"
#include "platform/Window.h"

namespace mc {

class BlockRegistry;
class ClientAssets;
class Font;
class GuiBatch;
class Gallery;
class Level;
class WorldRenderer;

class Game {
public:
    static constexpr double kTickSeconds = 0.05; // 20 ticks per second

    explicit Game(CliOptions options);
    ~Game();

    // Returns the process exit code.
    int run();

private:
    bool init();
    bool initWorld();
    void shutdown();
    void handleGlobalKeys();
    void updateCamera(double dt);
    void updateWorld();
    void tick();
    void render(float partialTick);
    void afterFrame(); // harness: screenshot / exit when the run conditions are met
    void runCommand(const std::string& line);
    void updateTitle();
    void renderDebugOverlay(glm::ivec2 fb);

    CliOptions opts_;
    Window window_;
    std::unique_ptr<JobSystem> jobs_;
    std::unique_ptr<ClientAssets> assets_;
    std::unique_ptr<BlockRegistry> registry_;
    std::unique_ptr<Gallery> gallery_;
    std::unique_ptr<Level> level_;
    std::unique_ptr<WorldRenderer> worldRenderer_;
    std::unique_ptr<GuiBatch> gui_;
    std::unique_ptr<Font> font_;
    bool showDebug_ = false;
    double fps_ = 0.0;
    Commands commands_;
    Camera camera_;

    long long frameCount_ = 0;
    long long tickCount_ = 0;
    bool harnessDone_ = false;
    bool execDone_ = false;
    bool screenshotRequested_ = false;
    bool toolOnly_ = false; // a --dump-* tool ran inside init()
    int exitCode_ = 0;
    Stopwatch titleTimer_;
    int framesSinceTitle_ = 0;
    Stopwatch runTimer_;
    bool meshedLogged_ = false;
};

} // namespace mc
