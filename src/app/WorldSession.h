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
#include "game/BlockEntities.h"
#include "game/ContainerMenus.h"
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
struct LevelEvent;
class BiomeDecorator;
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
    // The menu of the block the player opened, if its screen is up.
    Menu* openMenu() { return openMenu_.get(); }
    // A screen the world asks for outside input (the death screen, a block's menu, or
    // "close" when the open menu's block is gone), once.
    std::string takePendingScreen() { return std::exchange(pendingScreen_, {}); }
    // Back at the spawn with full health (spectating, in hardcore).
    void respawnPlayer();
    // Out of bed (the Leave Bed button, or morning).
    void wakeUp();
    // The sign being edited (for the edit screen) and which side.
    std::shared_ptr<SignBlockEntity> editingSign() const { return editingSign_; }
    bool editingFront() const { return editingFront_; }
    void stopEditingSign();
    bool hardcore() const { return params_.hardcore; }
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
    // The player died: the death message, drops and experience, then the death screen.
    void die();
    bool eatCake(const BlockPos& pos, StateId state);
    // A line above the hotbar (vanilla displayClientMessage to the action bar).
    void message(const std::string& key);
    // Right-click with armor in hand: it swaps with what is worn in its slot.
    bool equipFromHand(ItemStack& stack);
    // Buckets: fill from a source (or a waterlogged block), pour where aimed.
    bool useBucket(ItemStack& stack);
    bool emptyBucket(const BlockPos& pos, FluidKind content, const BlockHit* hit);
    // Breaking blocks and swinging at nothing (the attack button), once a tick.
    void tickAttack(bool attackHeld);
    // Signs: dyes, glow ink and wax by hand; otherwise the facing side opens for editing.
    bool useSign(const BlockPos& pos, StateId state);
    void editSign(const BlockPos& pos, bool front);
    float signYaw(StateId state) const;
    // Beds: sets the respawn point, and at night lies down in it.
    bool useBed(const BlockPos& pos, StateId state);
    void tickSleep();
    glm::dvec3 standUpPosition(const BlockPos& head) const;
    // Tools and other items used on a block (hoes, axes, flint and steel, ...).
    bool useHeldItemOn(const BlockPos& pos, Direction face);
    // Eating and drinking: started by use, held to the end, then the food counts.
    void startUsingItem();
    void continueUsingItem(bool held);
    // Right-click on a block with a menu (crafting table, ...): opens it.
    bool openBlockMenu(const BlockPos& pos, StateId state);
    // Closes the open menu when its block is gone or the player too far.
    void checkOpenMenu();
    // What breaking `state` with `held` drops (loot tables arrive in data/loot).
    std::vector<ItemStack> blockDrops(StateId state, const ItemStack& held);
    // A tool wearing out: its pieces fly (vanilla breakItem particles).
    void spawnItemBreak(const Item& item);
    void spawnBlockParticles(const BlockPos& p, StateId s, std::optional<Direction> face);
    // Vanilla BoneMealItem.addGrowthParticles: sparkles in the block, or spread around
    // grass, moss and water.
    void spawnBoneMealParticles(const BlockPos& p, int count);
    // Mobs (M12): the right click on one (feeding, shearing, spawn eggs), the bow's arrows
    // and release, and the particles of entity events (poof, hearts, crits, sweeps).
    bool useOnEntity(Entity& e);
    ItemStack* findArrows();
    void releaseBow(ItemStack& bow);
    void spawnEntityParticles(const LevelEvent& e);
    // The client's per-tick particles: the world's ambient ones, rain splashes, lit TNT
    // smoking, the player splashing into water.
    void tickClientParticles();
    bool placeBlock();
    void renderHud(glm::ivec2 fb, float partialTick, const std::function<void()>& renderGui);
    // Items, orbs and pickup animations for the entity renderer.
    void submitEntities(float partialTick, int skyDarken);
    // Vanilla GameRenderer.tryTakeScreenshotIfNeeded: once the whole view is drawn, the
    // middle square of it becomes the world list's 64x64 icon.png (if there is none).
    void captureIcon(glm::ivec2 fb);
    void renderDebugOverlay(GuiGraphics& gui);

    Client& client_;
    WorldParams params_;
    std::filesystem::path worldDir_; // empty for an unsaved world
    std::shared_ptr<const OverworldGenerator> overworld_; // when the world type is "default"
    std::shared_ptr<BiomeDecorator> decorator_;            // features (trees, flowers) for every world type
    std::unique_ptr<Level> level_;
    std::unique_ptr<WorldRenderer> worldRenderer_;
    std::unique_ptr<BlockOverlay> overlay_;
    std::unique_ptr<Player> player_;
    std::unique_ptr<InventoryMenu> inventoryMenu_;
    std::unique_ptr<Hud> hud_;
    ParticleSystem particles_;
    bool wasInWater_ = true; // no splash on the first tick
    int skyFlashTime_ = 0;    // lightning lights the sky (vanilla ClientLevel.skyFlashTime)
    int waterVisionTime_ = 0; // ticks the eyes have spent under water (vanilla, up to 600)
    Camera camera_;
    Perspective perspective_ = Perspective::FirstPerson;
    float eyeHeight_ = 1.62f, oldEyeHeight_ = 1.62f; // the camera's eased eye height
    float fovModifier_ = 1.0f, oldFovModifier_ = 1.0f; // vanilla GameRenderer.fovModifier
    std::optional<BlockHit> target_;                  // the block under the crosshair
    uint32_t entityTarget_ = 0;                       // or the entity (vanilla crosshairPickEntity)
    struct CritEmitter {
        glm::dvec3 pos;
        glm::dvec2 size; // the target's width and height
        int life = 0;
    };
    std::vector<CritEmitter> critEmitters_;
    struct Breaking {
        BlockPos pos;
        float progress = 0.0f;
        bool active = false;
    } breaking_;
    int destroyDelay_ = 0; // ticks before the next block may break (vanilla destroyDelay)
    int useDelay_ = 0;     // ticks before the next placement repeats while held
    int missTime_ = 0;     // ticks of no attacking after a swing at nothing (vanilla missTime)
    int jumpTapTimer_ = 0; // double-tap jump window (vanilla jumpTriggerTime)
    bool jumpWasDown_ = false;
    int attackClicks_ = 0, useClicks_ = 0, pickClicks_ = 0; // presses since the last tick
    long long tickCount_ = 0;
    bool assertionFailed_ = false;
    struct PickupAnimation {
        std::unique_ptr<Entity> entity; // as it was when picked up
        int count = 1;
        int life = 0; // ticks of 3
    };
    std::vector<PickupAnimation> pickups_;
    bool deathHandled_ = false;
    std::string pendingScreen_;
    std::unique_ptr<Menu> openMenu_;
    std::shared_ptr<SignBlockEntity> editingSign_;
    bool editingFront_ = true;
    std::optional<MenuAccess> menuAccess_;
    bool hasIcon_ = false;
};

} // namespace mc
