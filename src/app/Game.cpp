#include "app/Game.h"

#include <algorithm>
#include <cmath>
#include <format>

#include <GLFW/glfw3.h>

#include "core/Log.h"
#include "gfx/GL.h"
#include "gfx/Screenshot.h"
#include "game/Item.h"
#include "game/Placement.h"
#include "gfx/Shader.h"
#include "render/ClientAssets.h"
#include "render/Gallery.h"
#include "render/BlockOverlay.h"
#include "resources/Language.h"
#include "render/EntityRenderer.h"
#include "render/WorldRenderer.h"
#include "game/Menu.h"
#include "ui/GuiGraphics.h"
#include "ui/Hud.h"
#include "game/CreativeTabs.h"
#include "ui/CreativeScreen.h"
#include "ui/InventoryScreen.h"
#include "world/light/LightEngine.h"
#include "world/block/BlockBehavior.h"
#include "world/block/BlockRegistry.h"
#include "world/biome/Biome.h"
#include "world/gen/ChunkGenerator.h"
#include "world/gen/OverworldGenerator.h"
#include "world/gen/feature/BiomeDecorator.h"
#include "world/level/Environment.h"
#include "world/level/Level.h"
#include "world/storage/ChunkStorage.h"
#include "world/storage/WorldSave.h"

namespace mc {

Game::Game(CliOptions options) : opts_(std::move(options)) {}

Game::~Game() { shutdown(); }

bool Game::init() {
    std::error_code ec;
    std::filesystem::create_directories(opts_.gameDir / "logs", ec);
    log::setFile((opts_.gameDir / "logs" / "latest.log").string());
    log::info("Game directory: {}", std::filesystem::absolute(opts_.gameDir).string());
    log::info("Resource pack: {}", opts_.assetsDir.string());
    if (!std::filesystem::exists(opts_.assetsDir / "assets" / "minecraft")) {
        log::error("No assets/minecraft under {} (use --assets)", opts_.assetsDir.string());
        return false;
    }

    Shader::setDirectory(opts_.shaderDir);

    WindowConfig wc;
    wc.width = opts_.width;
    wc.height = opts_.height;
    wc.vsync = opts_.vsync.value_or(true);
    wc.forceX11 = opts_.forceX11;
#ifndef NDEBUG
    wc.debugContext = true;
#endif
    if (opts_.glDebug) wc.debugContext = true;
    if (!window_.create(wc)) return false;

    jobs_ = std::make_unique<JobSystem>(opts_.threads);
    log::info("Job system: {} worker thread(s)", jobs_->workerCount());

    assets_ = std::make_unique<ClientAssets>();
    if (!assets_->load(opts_.assetsDir)) return false;

    if (!opts_.dumpModelSheets.empty()) {
        const int failures = dumpModelSheets(*assets_, opts_.dumpModelSheets, opts_.goldenTiles);
        exitCode_ = failures == 0 ? 0 : 1;
        toolOnly_ = true;
        return true;
    }

    gui_ = std::make_unique<GuiGraphics>();
    if (!gui_->init(*assets_)) return false;
    hud_ = std::make_unique<Hud>();
    if (!opts_.dumpItemIcons.empty()) {
        exitCode_ = dumpItemIcons(*assets_, gui_->items(), opts_.dumpItemIcons) == 0 ? 0 : 1;
        toolOnly_ = true;
        return true;
    }
    showDebug_ = opts_.debugOverlay;

    if (opts_.gallery) {
        gallery_ = std::make_unique<Gallery>();
        if (!gallery_->init(*assets_)) return false;
        camera_.position = opts_.pos.value_or(gallery_->startPosition());
        const glm::vec2 look = opts_.look.value_or(glm::vec2(-45.0f, 35.0f));
        camera_.yaw = look.x;
        camera_.pitch = look.y;
        return true;
    }
    return initWorld();
}

bool Game::initWorld() {
    registry_ = std::make_unique<BlockRegistry>();
    std::string err;
    if (!registry_->build(assets_->blockModels(), assets_->blockAtlas(), opts_.dataDir, &err))
        log::warn("block registry: {}", err); // problems are logged; the world still runs

    lang_ = std::make_unique<Language>();
    if (!lang_->load(assets_->pack())) log::warn("no language file in the resource pack");
    items_ = std::make_unique<ItemRegistry>();
    if (!items_->load(assets_->pack(), *registry_, opts_.dataDir, &err)) log::warn("items: {}", err);
    log::info("Items: {}", items_->size());
    creativeTabs_ = std::make_unique<CreativeTabs>();
    if (!creativeTabs_->load(opts_.dataDir, *items_, &err)) log::warn("creative tabs: {}", err);

    biomes_ = std::make_unique<BiomeRegistry>();
    if (!biomes_->load(opts_.dataDir, opts_.assetsDir, &err)) {
        log::error("biomes: {}", err);
        return false;
    }
    // A saved world keeps its seed and type; a new one takes them from the options.
    std::optional<WorldInfo> info;
    worldType_ = opts_.worldType;
    worldSeed_ = opts_.seed;
    if (!opts_.world.empty()) {
        worldDir_ = opts_.gameDir / "saves" / opts_.world;
        info = readWorldInfo(worldDir_);
        if (info) {
            worldType_ = info->type;
            worldSeed_ = info->seed;
            log::info("Loading world '{}' (type {}, seed {})", opts_.world, worldType_, worldSeed_);
        } else {
            log::info("Creating world '{}' in {}", opts_.world, worldDir_.string());
        }
        opts_.worldType = worldType_;
        opts_.seed = worldSeed_;
    }
    std::shared_ptr<const ChunkGenerator> generator;
    if (opts_.worldType == "flat") {
        generator = std::make_shared<FlatGenerator>(*registry_);
    } else if (opts_.worldType == "debug") {
        generator = std::make_shared<DebugGenerator>(*registry_);
    } else {
        auto decorator = std::make_shared<BiomeDecorator>();
        if (!decorator->load(opts_.dataDir, *registry_, *biomes_, &err)) {
            int shown = 0;
            for (size_t start = 0; start < err.size() && shown < 30; ++shown) {
                const size_t end = err.find('\n', start);
                log::warn("features: {}", err.substr(start, end - start));
                if (end == std::string::npos) break;
                start = end + 1;
            }
        }
        overworld_ = std::make_shared<OverworldGenerator>(*registry_, *biomes_, opts_.seed, std::move(decorator));
        generator = overworld_;
    }
    level_ = std::make_unique<Level>(*registry_, generator, *jobs_, opts_.seed);
    if (!worldDir_.empty()) {
        level_->attachStorage(std::make_shared<ChunkStorage>(worldDir_, *registry_, biomes_.get()));
        if (info) level_->loadData(info->level);
    }
    level_->chunks().setLightEngine(std::make_shared<LightEngine>(*registry_));
    if (opts_.time) level_->setDayTime(*opts_.time);
    level_->daylightCycle = !opts_.freezeTime;
    worldRenderer_ = std::make_unique<WorldRenderer>(*assets_, *registry_, opts_.worldType == "default" ? biomes_.get() : nullptr);
    if (!worldRenderer_->init()) return false;
    worldRenderer_->mesherOptions().smoothLighting = opts_.smoothLighting;

    overlay_ = std::make_unique<BlockOverlay>();
    if (!overlay_->init(*assets_)) return false;

    // The player stands at the spawn point; --pos places the camera (the eye) instead.
    player_ = std::make_unique<Player>();
    player_->setMode(gameModeFromName(opts_.gameMode).value_or(GameMode::Survival));
    if (opts_.pos) {
        glm::dvec3 eye = *opts_.pos;
        if (std::isnan(eye.y)) { // --pos X,~,Z: 12 blocks above the terrain
            const int x = int(std::floor(eye.x)), z = int(std::floor(eye.z));
            eye.y = (overworld_ ? overworld_->terrainTop(x, z) + 1 : generator->spawnPoint().y) + 12.0;
        }
        player_->pos = eye - glm::dvec3(0.0, player_->eyeHeight(), 0.0);
        // The float eye height leaves noise (y -60.0000000047 for an eye at -58.38): snap it off.
        player_->pos.y = std::round(player_->pos.y * 1e6) / 1e6;
    } else {
        player_->pos = generator->spawnPoint();
    }
    player_->oldPos = player_->pos;
    const glm::vec2 look = opts_.look.value_or(glm::vec2(-45.0f, 25.0f));
    player_->setRotation(look.x, look.y);
    camera_.position = player_->eyePos();
    camera_.yaw = look.x;
    camera_.pitch = look.y;
    perspective_ = opts_.perspective == "back"    ? Perspective::ThirdPersonBack
                   : opts_.perspective == "front" ? Perspective::ThirdPersonFront
                                                  : Perspective::FirstPerson;
    if (!worldDir_.empty())
        if (auto saved = readPlayerFile(worldDir_)) {
            loadPlayer(*player_, *saved, *items_);
            camera_.position = player_->eyePos();
        }
    eyeHeight_ = oldEyeHeight_ = player_->eyeHeight();
    // Creative starts with a few building blocks until the creative inventory arrives (M8).
    if (player_->mode() == GameMode::Creative && player_->inventory.main[0].empty()) {
        const char* starter[9] = {"stone", "grass_block", "dirt", "cobblestone", "oak_planks", "oak_log", "glass",
                                  "torch", "oak_stairs"};
        for (int i = 0; i < 9; ++i)
            if (const Item* item = items_->find(starter[i])) player_->inventory.main[size_t(i)] = {item, item->maxStack};
    }
    camera_.farPlane = float(opts_.renderDistance * 16 + 64) * 1.5f;
    log::info("World: type {}, seed {}, render distance {}", generator->type(), opts_.seed, opts_.renderDistance);
    return true;
}

void Game::saveWorld(bool wait) {
    if (worldDir_.empty() || !level_ || !player_) return;
    const int chunks = level_->chunks().saveAll();
    std::string err;
    if (!writeWorldInfo(worldDir_, {opts_.world, worldSeed_, worldType_, level_->saveData()}, &err)) log::error("{}", err);
    if (!writePlayerFile(worldDir_, savePlayer(*player_), &err)) log::error("{}", err);
    if (wait) {
        while (level_->chunks().savesPending()) jobs_->waitIdle();
    }
    log::info("Saved world '{}' ({} chunks)", opts_.world, chunks);
}

void Game::shutdown() {
    closeScreen(); // menus hand their items back before the save
    saveWorld(true);
    // Order matters: jobs reference the registry and assets until the pool is joined.
    overlay_.reset();
    worldRenderer_.reset();
    player_.reset();
    items_.reset();
    level_.reset();
    overworld_.reset();
    screen_.reset();
    inventoryMenu_.reset();
    creativeTabs_.reset();
    hud_.reset();
    gui_.reset();
    gallery_.reset();
    jobs_.reset();
    biomes_.reset();
    registry_.reset();
    assets_.reset();
    window_.destroy();
}

int Game::run() {
    if (!init()) return 1;
    if (toolOnly_) return exitCode_;

    double last = Window::time();
    double accumulator = 0.0;
    runTimer_.reset();
    while (!window_.shouldClose()) {
        window_.pollEvents();
        handleGlobalKeys();

        const double now = Window::time();
        const double frameDt = std::min(now - last, 0.25); // after a stall, skip ahead instead of catching up
        last = now;
        accumulator += frameDt;
        int ticksThisFrame = 0;
        while (accumulator >= kTickSeconds && ticksThisFrame < 10) {
            tick();
            accumulator -= kTickSeconds;
            ++ticksThisFrame;
        }

        updateCamera(frameDt);
        updateWorld();
        jobs_->drainMain(6.0);
        render(float(accumulator / kTickSeconds));
        ++frameCount_;
        afterFrame();
        window_.swapBuffers();
        updateTitle();
    }
    if (worldRenderer_) {
        const auto& r = worldRenderer_->chunkRenderer().stats();
        log::info("Ran {} frames in {:.2f} s ({:.0f} fps avg); last frame drew {} of {} sections, {} draws, {} vertices",
                  frameCount_, runTimer_.seconds(), double(frameCount_) / std::max(runTimer_.seconds(), 1e-6), r.drawn,
                  r.sections, r.draws, r.vertices);
    }
    return exitCode_;
}

void Game::handleGlobalKeys() {
    const Input& in = window_.input();
    if (in.keyPressed(GLFW_KEY_F11)) window_.toggleFullscreen();
    if (in.keyPressed(GLFW_KEY_F2)) screenshotRequested_ = true;
    if (screen_) {
        handleScreenInput();
        return;
    }
    if (in.keyPressed(GLFW_KEY_F5)) perspective_ = Perspective((int(perspective_) + 1) % 3);
    if (in.keyPressed(GLFW_KEY_F3)) showDebug_ = !showDebug_;
    if (in.keyPressed(GLFW_KEY_E) && player_) {
        openInventory();
        return;
    }
    // F swaps the main and off hand (not for spectators).
    if (in.keyPressed(GLFW_KEY_F) && player_ && player_->mode() != GameMode::Spectator)
        std::swap(player_->inventory.selectedStack(), player_->inventory.offhand);
    if (in.keyPressed(GLFW_KEY_ESCAPE)) {
        if (window_.cursorCaptured()) window_.setCursorCaptured(false);
        else window_.setShouldClose(true);
    }
    if (in.mousePressed(GLFW_MOUSE_BUTTON_LEFT) && !window_.cursorCaptured()) {
        window_.setCursorCaptured(true);
        return; // the capturing click is not an attack
    }
    if (!window_.cursorCaptured()) return;
    attackClicks_ += in.mousePressed(GLFW_MOUSE_BUTTON_LEFT);
    useClicks_ += in.mousePressed(GLFW_MOUSE_BUTTON_RIGHT);
    pickClicks_ += in.mousePressed(GLFW_MOUSE_BUTTON_MIDDLE);
    if (player_) {
        int& selected = player_->inventory.selected;
        for (int i = 0; i < 9; ++i)
            if (in.keyPressed(GLFW_KEY_1 + i)) selected = i;
        if (const double scroll = in.scrollY(); scroll != 0.0) selected = ((selected - (scroll > 0 ? 1 : -1)) % 9 + 9) % 9;
    }
}

glm::vec2 Game::guiMouse() const {
    const glm::ivec2 fb = window_.framebufferSize(), win = window_.windowSize();
    const glm::dvec2 c = window_.input().cursor();
    const glm::vec2 px(float(c.x) * float(fb.x) / float(std::max(win.x, 1)), float(c.y) * float(fb.y) / float(std::max(win.y, 1)));
    return px / float(autoGuiScale(fb));
}

ScreenContext Game::screenContext() const {
    ScreenContext ctx;
    ctx.lang = lang_.get();
    ctx.items = items_.get();
    return ctx;
}

void Game::openScreen(std::unique_ptr<Screen> screen) {
    if (screen_) closeScreen();
    screen_ = std::move(screen);
    window_.setCursorCaptured(false);
    const glm::ivec2 fb = window_.framebufferSize();
    screenSize_ = glm::vec2(fb) / float(autoGuiScale(fb));
    screen_->init(screenSize_);
    lastGuiMouse_ = guiMouse();
    guiButtonsDown_ = {};
}

void Game::closeScreen() {
    if (!screen_) return;
    screen_->removed();
    screen_.reset();
    window_.setCursorCaptured(true);
}

void Game::openInventory() {
    if (!player_ || player_->mode() == GameMode::Spectator) return;
    if (!inventoryMenu_) inventoryMenu_ = std::make_unique<InventoryMenu>(player_->inventory);
    if (player_->mode() == GameMode::Creative)
        openScreen(std::make_unique<CreativeScreen>(screenContext(), *player_, *inventoryMenu_, *creativeTabs_));
    else
        openScreen(std::make_unique<InventoryScreen>(screenContext(), *inventoryMenu_, *player_));
}

void Game::handleScreenInput() {
    const Input& in = window_.input();
    const glm::vec2 mouse = guiMouse();
    for (const Input::KeyEvent& e : in.keyEvents()) {
        if (e.action == GLFW_RELEASE) continue;
        if (!screen_->keyPressed(e.key, e.scancode, e.mods) && e.key == GLFW_KEY_ESCAPE && e.action == GLFW_PRESS)
            screen_->close();
    }
    for (char32_t c : in.typedText()) screen_->charTyped(c);
    for (int b = 0; b < 3; ++b)
        if (in.mousePressed(b)) {
            screen_->mouseClicked(mouse, b, in.mods());
            guiButtonsDown_[size_t(b)] = true;
        }
    if (mouse != lastGuiMouse_)
        for (int b = 0; b < 3; ++b)
            if (guiButtonsDown_[size_t(b)] && in.mouseDown(b)) screen_->mouseDragged(mouse, b, mouse - lastGuiMouse_);
    for (int b = 0; b < 3; ++b)
        if (in.mouseReleased(b) && guiButtonsDown_[size_t(b)]) {
            screen_->mouseReleased(mouse, b, in.mods());
            guiButtonsDown_[size_t(b)] = false;
        }
    if (in.scrollY() != 0.0) screen_->mouseScrolled(mouse, in.scrollY());
    lastGuiMouse_ = mouse;
    if (screen_->closing()) closeScreen();
}

void Game::updateCamera(double dt) {
    const Input& in = window_.input();
    if (player_) {
        if (window_.cursorCaptured()) {
            constexpr float kSensitivity = 0.15f;
            const glm::dvec2 d = in.mouseDelta();
            player_->yaw += float(d.x) * kSensitivity;
            player_->pitch = std::clamp(player_->pitch + float(d.y) * kSensitivity, -90.0f, 90.0f);
        }
        return;
    }
    if (window_.cursorCaptured()) {
        constexpr float kSensitivity = 0.15f;
        const glm::dvec2 d = in.mouseDelta();
        camera_.yaw += float(d.x) * kSensitivity;
        camera_.pitch = std::clamp(camera_.pitch + float(d.y) * kSensitivity, -90.0f, 90.0f);
    }
    glm::vec3 move(0.0f);
    const glm::vec3 fwd = glm::normalize(glm::vec3(camera_.forward().x, 0.0f, camera_.forward().z));
    if (in.keyDown(GLFW_KEY_W)) move += fwd;
    if (in.keyDown(GLFW_KEY_S)) move -= fwd;
    if (in.keyDown(GLFW_KEY_D)) move += camera_.right();
    if (in.keyDown(GLFW_KEY_A)) move -= camera_.right();
    if (in.keyDown(GLFW_KEY_SPACE)) move.y += 1.0f;
    if (in.keyDown(GLFW_KEY_LEFT_SHIFT)) move.y -= 1.0f;
    if (glm::length(move) > 0.0f) {
        const double speed = in.keyDown(GLFW_KEY_LEFT_CONTROL) ? 40.0 : 10.0;
        camera_.position += glm::dvec3(glm::normalize(move)) * speed * dt;
    }
}

void Game::updateWorld() {
    if (!level_) return;
    ChunkManager& chunks = level_->chunks();
    chunks.setView(ChunkPos::of(BlockPos::containing(camera_.position)), opts_.renderDistance);
    chunks.update();
    worldRenderer_->update(chunks, *jobs_);
    if (!meshedLogged_ && chunks.viewMeshed() && worldRenderer_->idle()) {
        meshedLogged_ = true;
        const auto& r = worldRenderer_->chunkRenderer();
        log::info("View meshed after {:.2f} s: {} sections, {:.1f} MB of vertex pages", runTimer_.seconds(),
                  r.sectionCount(), double(r.gpuBytes()) / (1 << 20));
    }
    if (!execDone_ && chunks.viewReady()) {
        execDone_ = true;
        for (const auto& cmd : opts_.exec) runCommand(cmd);
        if (opts_.screen == "inventory") openInventory();
    }
}

void Game::runCommand(const std::string& line) {
    CommandSource src;
    src.level = level_.get();
    src.position = player_ ? player_->pos : camera_.position;
    src.rotation = player_ ? glm::vec2(player_->yaw, player_->pitch) : glm::vec2(camera_.yaw, camera_.pitch);
    src.feedback = [](const std::string& msg) { log::info("[command] {}", msg); };
    src.teleport = [this](const glm::dvec3& p, std::optional<glm::vec2> rot) {
        if (!player_) return;
        player_->pos = player_->oldPos = p;
        player_->velocity = glm::dvec3(0.0);
        if (rot) player_->setRotation(rot->x, rot->y);
    };
    src.assertionFailed = [this](const std::string& msg) {
        log::error("assertion failed: {}", msg);
        exitCode_ = 1;
    };
    src.give = [this](std::string_view id, int count) {
        const Item* item = items_ ? items_->find(id) : nullptr;
        if (!item || !player_) return false;
        player_->inventory.add(*item, count);
        return true;
    };
    src.setGameMode = [this](std::string_view mode) {
        auto m = gameModeFromName(mode);
        if (!m || !player_) return false;
        player_->setMode(*m);
        return true;
    };
    std::string err;
    if (!commands_.execute(src, line, &err)) log::warn("command '{}' failed: {}", line, err);
}

void Game::tick() {
    ++tickCount_;
    assets_->tick();
    if (level_ && player_) {
        // The player waits (vanilla: frozen) until the chunk around it is ready.
        const ChunkPos here = ChunkPos::of(BlockPos::containing(player_->pos));
        if (player_->noPhysics || level_->chunks().isReady(here)) {
            // Vanilla handles the mouse buttons before the entities tick.
            tickInteraction();
            player_->tick(*level_, readPlayerInput());
            particles_.tick(*level_);
        } else {
            player_->oldPos = player_->pos;
        }
        hud_->tick(*player_, [this](const ItemStack& st) { return screenContext().itemName(st); });
        worldRenderer_->tickHands(*player_);
        // Vanilla Camera.tick: the eye height eases toward the player's (crouching).
        oldEyeHeight_ = eyeHeight_;
        eyeHeight_ += (player_->eyeHeight() - eyeHeight_) * 0.5f;
    }
    attackClicks_ = useClicks_ = pickClicks_ = 0;
    if (screen_) screen_->tick();
    if (level_) level_->tick();
    // Autosave every 5 minutes, like vanilla.
    if (level_ && tickCount_ % 6000 == 0) saveWorld(false);
    if (worldRenderer_) worldRenderer_->tick();
}

// Vanilla Camera.setup: at the eyes, or for the F5 views up to 4 blocks behind them
// (in front of them, looking back) but short of the blocks in between.
void Game::setupCamera(float partialTick) {
    const glm::dvec3 feet = glm::mix(player_->oldPos, player_->pos, double(partialTick));
    camera_.position = feet + glm::dvec3(0.0, oldEyeHeight_ + (eyeHeight_ - oldEyeHeight_) * partialTick, 0.0);
    camera_.yaw = player_->yaw;
    camera_.pitch = player_->pitch;
    if (perspective_ == Perspective::FirstPerson) return;
    if (perspective_ == Perspective::ThirdPersonFront) {
        camera_.yaw += 180.0f;
        camera_.pitch = -camera_.pitch;
    }
    const glm::dvec3 forward(camera_.forward());
    camera_.position -= forward * cameraDistance(*level_, camera_.position, forward, 4.0);
}

void Game::render(float partialTick) {
    const glm::ivec2 fb = window_.framebufferSize();
    if (player_) {
        setupCamera(partialTick);
        updateTarget(partialTick);
    }
    if (worldRenderer_) {
        EntityRenderer& entities = worldRenderer_->entities();
        entities.begin(camera_.position);
        if (perspective_ != Perspective::FirstPerson)
            entities.submitPlayer(*player_, *level_, partialTick, skyDarkenLevel(level_->dayTime()));
        worldRenderer_->render(camera_, fb, opts_.renderDistance, *level_, partialTick);
        worldRenderer_->renderParticles(particles_, *level_, camera_, fb, partialTick);
        if (target_ && player_->mode() != GameMode::Spectator) {
            const int stage = breaking_.active && breaking_.pos == target_->pos ? int(breaking_.progress * 10.0f) - 1 : -1;
            overlay_->render(camera_, fb, target_->pos, registry_->outlineShape(level_->getBlock(target_->pos)), stage);
        }
        if (perspective_ == Perspective::FirstPerson) worldRenderer_->renderHands(*player_, *level_, fb, partialTick);
        renderHud(fb, partialTick);
    } else {
        glViewport(0, 0, fb.x, fb.y);
        glClearColor(120.0f / 255.0f, 167.0f / 255.0f, 1.0f, 1.0f); // vanilla daytime sky blue
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (gallery_) gallery_->render(camera_, fb);
    }
    if (showDebug_) renderDebugOverlay(fb);
}

bool Game::controlDown(int key, std::string_view action) const {
    if (window_.cursorCaptured() && window_.input().keyDown(key)) return true;
    return std::find(opts_.hold.begin(), opts_.hold.end(), action) != opts_.hold.end();
}

PlayerInput Game::readPlayerInput() {
    PlayerInput in;
    const bool jumpDown = controlDown(GLFW_KEY_SPACE, "jump");
    // Vanilla double-tap: a second jump press within 7 ticks toggles creative flight.
    if (jumpTapTimer_ > 0) --jumpTapTimer_;
    if (jumpDown && !jumpWasDown_) {
        if (jumpTapTimer_ == 0) {
            jumpTapTimer_ = 7;
        } else {
            in.doubleTapJump = true;
            jumpTapTimer_ = 0;
        }
    }
    jumpWasDown_ = jumpDown;
    in.forward = float(controlDown(GLFW_KEY_W, "forward")) - float(controlDown(GLFW_KEY_S, "back"));
    in.strafe = float(controlDown(GLFW_KEY_A, "left")) - float(controlDown(GLFW_KEY_D, "right"));
    in.jump = jumpDown;
    in.sneak = controlDown(GLFW_KEY_LEFT_SHIFT, "sneak");
    in.sprint = controlDown(GLFW_KEY_LEFT_CONTROL, "sprint");
    return in;
}

void Game::updateTarget(float partialTick) {
    target_.reset();
    if (!level_ || !player_) return;
    // Vanilla block_interaction_range: 4.5, plus 0.5 in creative; from the eyes in every view.
    const double reach = player_->mode() == GameMode::Creative ? 5.0 : 4.5;
    const glm::dvec3 eye =
        glm::mix(player_->oldPos, player_->pos, double(partialTick)) + glm::dvec3(0.0, player_->eyeHeight(), 0.0);
    target_ = raycastBlocks(*level_, eye, player_->viewVector(), reach);
}

// Debris (or, mining, a chip off `face`) with the block's particle texture.
void Game::spawnBlockParticles(const BlockPos& p, StateId s, std::optional<Direction> face) {
    const auto choices = registry_->modelChoices(s);
    if (choices.empty() || choices[0]->models.empty()) return;
    const Block& b = registry_->blockOf(s);
    // Vanilla tints the particles like the block's first tint index (but not grass blocks).
    uint32_t tint = 0xFFFFFF;
    if (b.tint.source != TintSource::None && b.id != "minecraft:grass_block")
        tint = resolveTint(b.tint, 0, assets_->defaultBiomeColors());
    const int sprite = choices[0]->models[0]->particleSprite;
    if (face) particles_.addBlockHit(*registry_, s, p, *face, sprite, tint);
    else particles_.addBlockBreak(*registry_, s, p, sprite, tint);
}

void Game::breakBlock(const BlockPos& p) {
    const StateId s = level_->getBlock(p);
    spawnBlockParticles(p, s, std::nullopt);
    // A waterlogged block leaves its water behind.
    const bool waterlogged = registry_->boolValue(s, "waterlogged") || registry_->blockOf(s).className == "water_plant";
    level_->setBlockAndUpdate(p, waterlogged ? registry_->defaultState("water") : BlockRegistry::kAirState);
}

bool Game::placeBlock() {
    if (!target_) return false;
    ItemStack& held = player_->inventory.selectedStack();
    if (held.empty() || !held.item->block) return false;
    const Block& block = *held.item->block;
    const bool consume = player_->mode() != GameMode::Creative;
    const StateId clicked = level_->getBlock(target_->pos);
    const double hitY = target_->point.y - double(target_->pos.y);
    // A slab clicked on its open half becomes a double slab.
    if (auto merged = mergeIntoClicked(*registry_, clicked, block, target_->face, hitY)) {
        if (!level_->setBlockAndUpdate(target_->pos, *merged)) return false;
        if (consume) player_->inventory.consumeSelected();
        return true;
    }
    // Clicking a replaceable block (grass, snow layer) places into it; otherwise against the face.
    const bool replace = (registry_->info(clicked).flags & kReplaceable) && registry_->info(clicked).block != block.index;
    PlaceContext ctx;
    ctx.pos = replace ? target_->pos : target_->pos.offset(target_->face);
    ctx.clickedFace = target_->face;
    ctx.hitPoint = target_->point;
    ctx.yaw = player_->yaw;
    ctx.pitch = player_->pitch;
    ctx.replacingClicked = replace;
    ctx.sneaking = player_->crouching;
    const StateId there = level_->getBlock(ctx.pos);
    if (!(registry_->info(there).flags & (kReplaceable | kAir))) {
        // The neighbor may be a slab this one completes.
        auto merged = mergeIntoClicked(*registry_, there, block, opposite(target_->face), hitY);
        if (!merged || !level_->setBlockAndUpdate(ctx.pos, *merged)) return false;
        if (consume) player_->inventory.consumeSelected();
        return true;
    }
    const auto blocks = placementStates(*registry_, *level_, block, ctx);
    if (blocks.empty()) return false;
    // Never inside the player.
    const AABB body = player_->box().inflated(-1e-7, -1e-7, -1e-7);
    for (const auto& [p, s] : blocks)
        for (const AABB& b : registry_->collisionShape(s))
            if (b.moved(glm::dvec3(p.vec())).intersects(body)) return false;
    for (const auto& [p, s] : blocks) level_->setBlockAndUpdate(p, s);
    if (consume) player_->inventory.consumeSelected();
    return true;
}

void Game::tickInteraction() {
    if (!target_ || player_->mode() == GameMode::Spectator) {
        breaking_.active = false;
    }
    if (destroyDelay_ > 0) --destroyDelay_;
    if (useDelay_ > 0) --useDelay_;
    const Input& in = window_.input();
    const bool captured = window_.cursorCaptured();
    const bool attackHeld = (captured && in.mouseDown(GLFW_MOUSE_BUTTON_LEFT)) || controlDown(-1, "attack");
    const bool useHeld = (captured && in.mouseDown(GLFW_MOUSE_BUTTON_RIGHT)) || controlDown(-1, "use");
    // Pick block (creative): the target's block into the selected slot.
    if (pickClicks_ > 0 && target_ && player_->mode() == GameMode::Creative)
        if (const Item* item = items_->forBlock(registry_->blockOf(level_->getBlock(target_->pos))))
            player_->inventory.pick(*item);
    // Use: on click, then every 4 ticks while held (vanilla rightClickDelay). Blocks that
    // react to a hand (doors, levers, ...) take the click unless sneaking with an item.
    if (target_ && player_->mode() != GameMode::Spectator &&
        (useClicks_ > 0 || (useHeld && useDelay_ == 0))) {
        const StateId s = level_->getBlock(target_->pos);
        const bool bypass = player_->crouching && !player_->inventory.selectedStack().empty();
        const auto use = bypass ? std::nullopt : useBlock(*registry_, s, horizontalFacing(player_->yaw));
        // A successful use swings the arm.
        if (use) {
            if (use->state != s) level_->setBlockAndUpdate(target_->pos, use->state);
            if (use->tickDelay > 0) level_->scheduleTick(target_->pos, registry_->info(use->state).block, use->tickDelay);
            player_->swing();
        } else if (placeBlock()) {
            player_->swing();
        }
        useDelay_ = 4;
    }
    // Attack: creative breaks at once (5-tick pause between blocks); survival accumulates progress.
    // A click swings even at nothing; holding on a block swings every tick (vanilla continueAttack).
    const bool attacking = attackClicks_ > 0 || attackHeld;
    if (attackClicks_ > 0 && player_->mode() != GameMode::Spectator) player_->swing();
    if (!attacking || !target_ || player_->mode() == GameMode::Spectator || player_->mode() == GameMode::Adventure) {
        breaking_.active = false;
        return;
    }
    player_->swing();
    const BlockPos p = target_->pos;
    const StateId s = level_->getBlock(p);
    const Block& block = registry_->blockOf(s);
    if (player_->mode() == GameMode::Creative) {
        if (destroyDelay_ == 0 || attackClicks_ > 0) {
            breakBlock(p);
            destroyDelay_ = 5;
        }
        return;
    }
    if (destroyDelay_ > 0) return;
    if (!breaking_.active || breaking_.pos != p) {
        breaking_ = {p, 0.0f, true};
    }
    breaking_.progress += destroyProgressPerTick(block, *player_);
    spawnBlockParticles(p, s, target_->face);
    if (breaking_.progress >= 1.0f) {
        breakBlock(p);
        breaking_.active = false;
        destroyDelay_ = 5;
    }
}

void Game::renderHud(glm::ivec2 fb, float partialTick) {
    if (!player_) return;
    gui_->begin(fb, autoGuiScale(fb));
    const glm::dvec3 eye = camera_.position;
    const bool eyeInWater = perspective_ == Perspective::FirstPerson &&
                            level_->fluid(BlockPos::containing(eye)).kind == FluidKind::Water;
    ItemModelContext items;
    items.timeOfDay = worldRenderer_->environment().timeOfDay;
    items.holderPos = player_->pos;
    items.holderYaw = player_->yaw;
    items.spawnPos = glm::dvec3(level_->generator().spawnPoint());
    items.spin = float(tickCount_ % 200) / 200.0f;
    gui_->setItemContext(items);
    hud_->render(*gui_, *player_, perspective_ == Perspective::FirstPerson, eyeInWater,
                 [this](const ItemStack& stack, float x, float y) {
                     gui_->item(stack, x, y);
                     gui_->itemDecorations(stack, x, y);
                 });
    if (screen_) {
        // Re-lay out after a resize.
        if (gui_->size() != screenSize_) {
            screenSize_ = gui_->size();
            screen_->init(screenSize_);
        }
        screen_->render(*gui_, guiMouse(), partialTick);
    }
    gui_->end();
}

void Game::renderDebugOverlay(glm::ivec2 fb) {
    std::vector<std::string> lines;
    lines.push_back("Minecraft 1.21.8 (minecraft-cpp)");
    const glm::dvec3& p = camera_.position;
    if (level_) {
        const auto& r = worldRenderer_->chunkRenderer().stats();
        lines.push_back(std::format("{:.0f} fps  C: {}/{} sections, {} draws", fps_, r.drawn, r.sections, r.draws));
        const auto st = level_->chunks().stats();
        lines.push_back(std::format("Chunks: {} loaded, {} lit, {} ready, {} jobs", st.entries, st.lit, st.meshReady,
                                    st.inFlight));
        lines.push_back("");
        lines.push_back(std::format("XYZ: {:.3f} / {:.5f} / {:.3f}", p.x, p.y, p.z));
        const BlockPos b = BlockPos::containing(p);
        lines.push_back(std::format("Block: {} {} {} [{} {} {}]", b.x, b.y, b.z, b.x & 15, b.y & 15, b.z & 15));
        lines.push_back(std::format("Chunk: {} {} {} in {} {} {}", b.x & 15, b.y & 15, b.z & 15, b.x >> 4, b.y >> 4, b.z >> 4));
        static const char* names[4] = {"south (Towards positive Z)", "west (Towards negative X)",
                                       "north (Towards negative Z)", "east (Towards positive X)"};
        const int facing = int(std::floor(std::fmod(std::fmod(camera_.yaw, 360.0f) + 360.0f + 45.0f, 360.0f) / 90.0f)) & 3;
        lines.push_back(std::format("Facing: {} ({:.1f} / {:.1f})", names[facing], std::remainder(camera_.yaw, 360.0f),
                                    camera_.pitch));
        if (const Chunk* c = level_->chunks().chunk(ChunkPos::of(b))) {
            const uint8_t l = c->lightAt(b.x & 15, b.y, b.z & 15);
            lines.push_back(std::format("Client Light: {} ({} sky, {} block)", std::max(l >> 4, l & 15), l >> 4, l & 15));
            if (overworld_ && b.y >= kWorldMinY && b.y <= kWorldMaxY) {
                const Section& sec = c->sections[size_t(sectionIndexOfY(b.y))];
                const BiomeId id = sec.biomes[size_t(((((b.y - kWorldMinY) & 15) >> 2) << 4) | (((b.z & 15) >> 2) << 2) | ((b.x & 15) >> 2))];
                lines.push_back(std::format("Biome: {}", biomes_->get(id).id));
            }
        }
        const auto& env = worldRenderer_->environment();
        lines.push_back(std::format("Day {} Time {} (celestial {:.3f}, sky darken {:.2f})",
                                    level_->dayTime() / 24000, level_->dayTime() % 24000, env.timeOfDay, env.skyDarken));
        if (player_) {
            lines.push_back(std::format("Mode: {}{}{}", gameModeName(player_->mode()), player_->flying ? ", flying" : "",
                                        player_->onGround ? ", on ground" : ""));
            if (target_) {
                const BlockPos& t = target_->pos;
                lines.push_back("");
                lines.push_back(std::format("Targeted Block: {}, {}, {} ({})", t.x, t.y, t.z, directionName(target_->face)));
                const std::string desc = registry_->describe(level_->getBlock(t));
                const size_t bracket = desc.find('[');
                lines.push_back(desc.substr(0, bracket));
                if (bracket != std::string::npos) {
                    // One property per line, like vanilla.
                    std::string props = desc.substr(bracket + 1, desc.size() - bracket - 2);
                    for (size_t start = 0; start <= props.size();) {
                        const size_t comma = props.find(',', start);
                        std::string kv = props.substr(start, comma - start);
                        if (const size_t eq = kv.find('='); eq != std::string::npos) kv = kv.substr(0, eq) + ": " + kv.substr(eq + 1);
                        lines.push_back(kv);
                        if (comma == std::string::npos) break;
                        start = comma + 1;
                    }
                }
            }
        }
    } else {
        lines.push_back(std::format("{:.0f} fps", fps_));
        lines.push_back(std::format("XYZ: {:.3f} / {:.5f} / {:.3f}", p.x, p.y, p.z));
    }
    gui_->begin(fb, autoGuiScale(fb));
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].empty()) continue;
        const float y = 2.0f + 9.0f * float(i);
        gui_->fill(1, y - 1, 2.0f + float(gui_->textWidth(lines[i])) + 1.0f, y + 8, 0x90505050);
        gui_->text(lines[i], 2, y, 0xFFE0E0E0, false);
    }
    gui_->end();
}

void Game::afterFrame() {
    const glm::ivec2 fb = window_.framebufferSize();
    if (screenshotRequested_) {
        screenshotRequested_ = false;
        saveScreenshot(nextScreenshotPath(opts_.gameDir / "screenshots"), fb.x, fb.y);
    }
    if (harnessDone_) return;
    const bool harness = !opts_.screenshot.empty() || opts_.exitAfter;
    if (!harness || frameCount_ < std::max(opts_.frames, 1) || tickCount_ < opts_.ticks) return;
    if (level_ && (!opts_.exec.empty() || opts_.waitChunks)) {
        if (!execDone_) return;
        if (opts_.waitChunks && !(level_->chunks().viewMeshed() && worldRenderer_->idle())) return;
    }
    harnessDone_ = true;
    if (!opts_.screenshot.empty()) saveScreenshot(opts_.screenshot, fb.x, fb.y);
    if (opts_.exitAfter) window_.setShouldClose(true);
}

void Game::updateTitle() {
    ++framesSinceTitle_;
    if (titleTimer_.seconds() < 1.0) return;
    const double fps = framesSinceTitle_ / titleTimer_.seconds();
    fps_ = fps;
    framesSinceTitle_ = 0;
    titleTimer_.reset();
    std::string title = std::format("Minecraft  |  {:.0f} fps", fps);
    if (level_) {
        const auto s = level_->chunks().stats();
        const auto& r = worldRenderer_->chunkRenderer().stats();
        title += std::format("  |  chunks {} (lit {}, ready {}, jobs {})  |  sections {}/{}  |  {:.0f} MB VRAM", s.entries,
                             s.lit, s.meshReady, s.inFlight, r.drawn, r.sections,
                             double(worldRenderer_->chunkRenderer().gpuBytes()) / (1 << 20));
        title += std::format("  |  {:.1f} {:.1f} {:.1f}", camera_.position.x, camera_.position.y, camera_.position.z);
    }
    window_.setTitle(title);
}

} // namespace mc
