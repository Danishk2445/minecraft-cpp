// WorldSession.h -- one open world: its level and generator, the player and
// what they are doing (movement, breaking, placing), the camera and every
// renderer of the world, the HUD and the F3 screen. The client (window, assets,
// registries, GUI) outlives it (app/Client.h).
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "entity/Particles.h"
#include "entity/Player.h"
#include "game/WorldParams.h"
#include "gfx/Camera.h"
#include "ui/Screen.h"
#include "world/level/Raycast.h"

namespace mc {

struct Client;
class BlockOverlay;
class GuiGraphics;
class Hud;
class InventoryMenu;
class Level;
class OverworldGenerator;
class WorldRenderer;

class WorldSession {
public:
    static constexpr int kAutosaveTicks = 6000; // vanilla: every 5 minutes

    WorldSession(Client& client, WorldParams params);
    ~WorldSession(); // saves and waits for the writes
    bool init();

    // Main thread, every frame: mouse look (when captured) and chunk streaming.
    void frame(double dt);
    // Keys and clicks while no screen is open. Returns a screen request: "inventory",
    // "pause", "chat", "command" (chat opened with '/') or empty.
    std::string handleInput();
    // 20 Hz.
    void tick();
    // The world, the hands and the HUD (`renderGui` lets screens draw before the GUI pass ends).
    void render(glm::ivec2 framebuffer, float partialTick, const std::function<void()>& renderGui);
    void save(bool wait);

    // Commands and chat lines; feedback goes to `reply`.
    void runCommand(const std::string& line, const std::function<void(const std::string&)>& reply);

    Player& player() { return *player_; }
    Level& level() { return *level_; }
    InventoryMenu& inventoryMenu();
    const Camera& camera() const { return camera_; }
    // The spawn area is generated and meshed (the loading screen waits for it).
    bool viewReady() const;
    bool viewMeshed() const;
    // Fraction of the spawn area ready, for the loading screen.
    float loadProgress() const;
    // The loading screen's progress and chunk map.
    WorldLoadStatus loadStatus() const;
    // Puts the player's eyes (and the camera) at `eye`, looking yaw/pitch.
    void setPose(const glm::dvec3& eye, float yaw, float pitch);
    // Only the world (no hands, outline, particles or HUD) from `camera` into the bound
    // framebuffer, `size` pixels: the panorama's faces.
    void renderScenery(const Camera& camera, glm::ivec2 size);
    long long ticks() const { return tickCount_; }
    const std::string& folder() const { return params_.folder; }
    Difficulty difficulty() const { return params_.difficulty; }
    void setDifficulty(Difficulty d) { params_.difficulty = d; }
    bool showDebug = false;
    double fps = 0.0;
    std::string statusLine() const; // for the window title
    // An /assertblock check failed (the harness exits with an error).
    bool assertionFailed() const { return assertionFailed_; }

private:
    enum class Perspective : uint8_t { FirstPerson, ThirdPersonBack, ThirdPersonFront };

    // Video options the renderers read (a smooth lighting change remeshes everything).
    void applyOptions();
    void setupCamera(float partialTick);
    void updateTarget(float partialTick);
    PlayerInput readPlayerInput();
    bool controlDown(int key, std::string_view action) const;
    void tickInteraction();
    void breakBlock(const BlockPos& p);
    void spawnBlockParticles(const BlockPos& p, StateId s, std::optional<Direction> face);
    bool placeBlock();
    void renderHud(glm::ivec2 fb, float partialTick, const std::function<void()>& renderGui);
    // Vanilla GameRenderer.tryTakeScreenshotIfNeeded: once the whole view is drawn, the
    // middle square of it becomes the world list's 64x64 icon.png (if there is none).
    void captureIcon(glm::ivec2 fb);
    void renderDebugOverlay(GuiGraphics& gui);

    Client& client_;
    WorldParams params_;
    std::filesystem::path worldDir_; // empty for an unsaved world
    std::shared_ptr<const OverworldGenerator> overworld_; // when the world type is "default"
    std::unique_ptr<Level> level_;
    std::unique_ptr<WorldRenderer> worldRenderer_;
    std::unique_ptr<BlockOverlay> overlay_;
    std::unique_ptr<Player> player_;
    std::unique_ptr<InventoryMenu> inventoryMenu_;
    std::unique_ptr<Hud> hud_;
    ParticleSystem particles_;
    Camera camera_;
    Perspective perspective_ = Perspective::FirstPerson;
    float eyeHeight_ = 1.62f, oldEyeHeight_ = 1.62f; // the camera's eased eye height
    std::optional<BlockHit> target_;                  // the block under the crosshair
    struct Breaking {
        BlockPos pos;
        float progress = 0.0f;
        bool active = false;
    } breaking_;
    int destroyDelay_ = 0; // ticks before the next block may break (vanilla destroyDelay)
    int useDelay_ = 0;     // ticks before the next placement repeats while held
    int jumpTapTimer_ = 0; // double-tap jump window (vanilla jumpTriggerTime)
    bool jumpWasDown_ = false;
    int attackClicks_ = 0, useClicks_ = 0, pickClicks_ = 0; // presses since the last tick
    long long tickCount_ = 0;
    bool assertionFailed_ = false;
    bool hasIcon_ = false;
};

} // namespace mc
