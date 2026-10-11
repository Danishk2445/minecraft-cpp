// Game.h -- owns the window and subsystems and runs the fixed-tick main loop.
#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <optional>

#include "app/Cli.h"
#include "core/JobSystem.h"
#include "entity/Particles.h"
#include "entity/Player.h"
#include "world/level/Raycast.h"
#include "core/Timer.h"
#include "game/Commands.h"
#include "gfx/Camera.h"
#include "platform/Window.h"

namespace mc {

class BiomeRegistry;
class BlockRegistry;
class OverworldGenerator;
class ClientAssets;
class Font;
class GuiBatch;
class Gallery;
class Level;
class WorldRenderer;
class BlockOverlay;
class ItemRegistry;
class Language;

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
    void renderHud(glm::ivec2 fb);
    PlayerInput readPlayerInput();
    // A key or button held down (with the cursor captured), or the harness's --hold action.
    bool controlDown(int key, std::string_view action) const;
    void tickInteraction();
    void breakBlock(const BlockPos& p);
    void spawnBlockParticles(const BlockPos& p, StateId s, std::optional<Direction> face);
    bool placeBlock(); // true when a block was placed
    void updateTarget(float partialTick);
    void setupCamera(float partialTick);
    void saveWorld(bool wait);

    CliOptions opts_;
    Window window_;
    std::unique_ptr<JobSystem> jobs_;
    std::unique_ptr<ClientAssets> assets_;
    std::unique_ptr<BlockRegistry> registry_;
    std::unique_ptr<BiomeRegistry> biomes_;
    std::unique_ptr<ItemRegistry> items_;
    std::unique_ptr<Language> lang_;
    std::shared_ptr<const OverworldGenerator> overworld_; // when the world type is "default"
    std::unique_ptr<Gallery> gallery_;
    std::unique_ptr<Level> level_;
    std::unique_ptr<WorldRenderer> worldRenderer_;
    std::unique_ptr<GuiBatch> gui_;
    std::unique_ptr<Font> font_;
    bool showDebug_ = false;
    double fps_ = 0.0;
    Commands commands_;
    Camera camera_;
    // F5 cycles first person, third person from behind, third person from the front.
    enum class Perspective : uint8_t { FirstPerson, ThirdPersonBack, ThirdPersonFront };
    Perspective perspective_ = Perspective::FirstPerson;
    float eyeHeight_ = 1.62f, oldEyeHeight_ = 1.62f; // the camera's eased eye height
    std::unique_ptr<Player> player_;
    ParticleSystem particles_;
    std::unique_ptr<BlockOverlay> overlay_;
    std::optional<BlockHit> target_; // the block under the crosshair
    struct Breaking {
        BlockPos pos;
        float progress = 0.0f;
        bool active = false;
    } breaking_;
    int destroyDelay_ = 0;  // ticks before the next block may break (vanilla destroyDelay)
    int useDelay_ = 0;      // ticks before the next placement repeats while held
    int jumpTapTimer_ = 0;  // double-tap jump window (vanilla jumpTriggerTime)
    bool jumpWasDown_ = false;
    int attackClicks_ = 0, useClicks_ = 0, pickClicks_ = 0; // presses since the last tick
    std::filesystem::path worldDir_; // empty for an unsaved world
    std::string worldType_;
    int64_t worldSeed_ = 0;

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
