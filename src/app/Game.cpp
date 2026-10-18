#include "app/Game.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <format>
#include <random>
#include <thread>

#include <GLFW/glfw3.h>

#include "app/WorldSession.h"
#include "core/Fs.h"
#include "core/Log.h"
#include "game/CreativeTabs.h"
#include "game/Item.h"
#include "entity/mob/EntityTypes.h"
#include "game/Loot.h"
#include "game/Menu.h"
#include "game/Recipes.h"
#include "gfx/GL.h"
#include "gfx/Screenshot.h"
#include "gfx/Shader.h"
#include "render/ClientAssets.h"
#include "render/Gallery.h"
#include "resources/Language.h"
#include "ui/Chat.h"
#include "ui/ContainerScreens.h"
#include "ui/CreativeScreen.h"
#include "ui/DeathScreen.h"
#include "ui/GameMenuScreens.h"
#include "ui/GuiGraphics.h"
#include "ui/InventoryScreen.h"
#include "ui/SignEditScreen.h"
#include "ui/TitleScreens.h"
#include "world/biome/Biome.h"
#include "world/block/BlockRegistry.h"
#include "world/level/Level.h"

namespace mc {
namespace {

// The baked panorama: mid-morning over a river valley with a cherry grove on a hill.
constexpr int64_t kPanoramaSeed = 42;
constexpr glm::dvec3 kPanoramaEye{-60.0, 79.0, -250.0};
constexpr float kPanoramaYaw = 0.0f;
constexpr long long kPanoramaTime = 2500;
constexpr int kPanoramaSize = 1024;
constexpr int kPanoramaRenderDistance = 10;

// Java String.hashCode (the splash list leaves out one line by its hash).
int32_t javaHash(std::string_view s) {
    uint32_t h = 0;
    for (char c : s) h = 31 * h + uint32_t(static_cast<unsigned char>(c));
    return int32_t(h);
}

} // namespace

int Client::guiScale() const {
    const int automatic = autoGuiScale(window.framebufferSize());
    return options.guiScale > 0 ? std::min(options.guiScale, automatic) : automatic;
}

glm::vec2 Client::guiMouse() const {
    const glm::ivec2 fb = window.framebufferSize(), win = window.windowSize();
    const glm::dvec2 c = window.input().cursor();
    const glm::vec2 px(float(c.x) * float(fb.x) / float(std::max(win.x, 1)),
                       float(c.y) * float(fb.y) / float(std::max(win.y, 1)));
    return px / float(guiScale());
}

Game::Game(CliOptions options) { client_.cli = std::move(options); }

Game::~Game() { shutdown(); }

bool Game::init() {
    const CliOptions& cli = client_.cli;
    std::error_code ec;
    std::filesystem::create_directories(cli.gameDir / "logs", ec);
    log::setFile((cli.gameDir / "logs" / "latest.log").string());
    log::info("Game directory: {}", std::filesystem::absolute(cli.gameDir).string());
    log::info("Resource pack: {}", cli.assetsDir.string());
    if (!std::filesystem::exists(cli.assetsDir / "assets" / "minecraft")) {
        log::error("No assets/minecraft under {} (use --assets)", cli.assetsDir.string());
        return false;
    }
    // Options, with the command line's overrides for this run.
    std::string err;
    client_.options = Options::load(cli.gameDir / "options.json", &err);
    if (!err.empty()) log::warn("{}", err);
    if (cli.renderDistance) client_.options.renderDistance = *cli.renderDistance;
    if (cli.smoothLighting) client_.options.smoothLighting = *cli.smoothLighting;
    if (cli.vsync) client_.options.vsync = *cli.vsync;

    Shader::setDirectory(cli.shaderDir);
    WindowConfig wc;
    wc.width = cli.width;
    wc.height = cli.height;
    wc.vsync = client_.options.vsync;
    wc.forceX11 = cli.forceX11;
#ifndef NDEBUG
    wc.debugContext = true;
#endif
    if (cli.glDebug) wc.debugContext = true;
    if (!client_.window.create(wc)) return false;

    client_.jobs = std::make_unique<JobSystem>(cli.threads);
    log::info("Job system: {} worker thread(s)", client_.jobs->workerCount());

    client_.assets = std::make_unique<ClientAssets>();
    if (!client_.assets->load(cli.assetsDir)) return false;

    if (!cli.dumpModelSheets.empty()) {
        const int failures = dumpModelSheets(*client_.assets, cli.dumpModelSheets, cli.goldenTiles);
        exitCode_ = failures == 0 ? 0 : 1;
        toolOnly_ = true;
        return true;
    }
    client_.gui = std::make_unique<GuiGraphics>();
    if (!client_.gui->init(*client_.assets)) return false;
    client_.chat = std::make_unique<Chat>();
    if (!cli.dumpItemIcons.empty()) {
        exitCode_ = dumpItemIcons(*client_.assets, client_.gui->items(), cli.dumpItemIcons) == 0 ? 0 : 1;
        toolOnly_ = true;
        return true;
    }
    if (cli.gallery) {
        gallery_ = std::make_unique<Gallery>();
        if (!gallery_->init(*client_.assets)) return false;
        galleryCamera_.position = cli.pos.value_or(gallery_->startPosition());
        const glm::vec2 look = cli.look.value_or(glm::vec2(-45.0f, 35.0f));
        galleryCamera_.yaw = look.x;
        galleryCamera_.pitch = look.y;
        return true;
    }
    if (!loadRegistries()) return false;
    if (cli.bakePanorama) {
        exitCode_ = bakePanorama(cli.gameDir / "panorama") ? 0 : 1;
        toolOnly_ = true;
        return true;
    }
    if (!cli.play.empty()) {
        // As if picked in the world list: the title, then the loading screen, then the game.
        loadSplash();
        loadPanorama();
        openTitle();
        WorldParams params;
        params.folder = params.name = cli.play;
        params.type = cli.worldType;
        params.seed = cli.seed;
        params.gameMode = cli.gameMode;
        startWorld(params);
        applyPending();
        return world_ != nullptr;
    }
    if (cli.worldRequested) {
        // The harness's world, opened at once.
        WorldParams params;
        params.folder = cli.world;
        params.name = cli.world.empty() ? "New World" : cli.world;
        params.type = cli.worldType;
        params.seed = cli.seed;
        params.gameMode = cli.gameMode;
        params.commandLine = true;
        startWorld(params);
        applyPending();
        return world_ != nullptr;
    }
    loadSplash();
    loadPanorama();
    openTitle();
    openHarnessScreen();
    applyPending();
    return true;
}

void Game::openTitle() { setScreen(std::make_unique<TitleScreen>(screenContext())); }

void Game::loadSplash() {
    if (client_.cli.splash) {
        splash_ = *client_.cli.splash;
        return;
    }
    // Vanilla SplashManager: three days have their own; otherwise a random line.
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    const int month = tm.tm_mon + 1, day = tm.tm_mday;
    if (month == 12 && day == 24) splash_ = "Merry X-mas!";
    else if (month == 1 && day == 1) splash_ = "Happy new year!";
    else if (month == 10 && day == 31) splash_ = "OOoooOOOoooo! Spooky!";
    if (!splash_.empty()) return;
    std::vector<std::string> lines;
    if (auto text = fs::readText(client_.assets->pack().file("minecraft", "texts/splashes.txt"))) {
        size_t start = 0;
        while (start < text->size()) {
            size_t end = text->find('\n', start);
            if (end == std::string::npos) end = text->size();
            std::string line = text->substr(start, end - start);
            start = end + 1;
            while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
            size_t first = 0;
            while (first < line.size() && std::isspace(static_cast<unsigned char>(line[first]))) ++first;
            line.erase(0, first);
            // "This message will never appear on the splash screen, isn't that weird?"
            if (!line.empty() && javaHash(line) != 125780783) lines.push_back(std::move(line));
        }
    }
    if (lines.empty()) return;
    std::mt19937 rng(std::random_device{}());
    splash_ = lines[std::uniform_int_distribution<size_t>(0, lines.size() - 1)(rng)];
}

void Game::loadPanorama() {
    const std::filesystem::path dir = client_.cli.gameDir / "panorama";
    panorama_ = std::make_unique<Panorama>();
    if (panorama_->load(dir)) return;
    if (bakePanorama(dir) && panorama_->load(dir)) return;
    log::warn("No title panorama: the menus get a plain background");
}

void Game::presentScreen(Screen& s) {
    client_.window.pollEvents();
    const glm::ivec2 fb = client_.window.framebufferSize();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fb.x, fb.y);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    GuiGraphics& gui = *client_.gui;
    gui.begin(fb, client_.guiScale());
    s.init(gui.size());
    s.render(gui, client_.guiMouse(), 0.0f);
    gui.end();
    client_.window.swapBuffers();
}

bool Game::bakePanorama(const std::filesystem::path& dir) {
    log::info("Baking the title panorama into {}", dir.string());
    Stopwatch timer;
    const int renderDistance = client_.options.renderDistance;
    client_.options.renderDistance = kPanoramaRenderDistance;
    bool ok = false;
    {
        // An unsaved world (no folder). --seed, --pos, --look and --time choose another view.
        const CliOptions& cli = client_.cli;
        WorldParams params;
        params.name = "Panorama";
        params.seed = cli.worldRequested ? cli.seed : kPanoramaSeed;
        params.gameMode = "spectator";
        params.commandLine = cli.worldRequested;
        WorldSession world(client_, params);
        if (world.init()) {
            if (!params.commandLine || !cli.time) world.level().setDayTime(kPanoramaTime);
            world.level().daylightCycle = false;
            glm::dvec3 eye = kPanoramaEye;
            float yaw = kPanoramaYaw;
            if (params.commandLine && cli.pos) {
                eye = world.camera().position;
                yaw = world.player().yaw;
            }
            world.setPose(eye, yaw, 0.0f);
            log::info("Panorama view from {:.2f} {:.2f} {:.2f}, yaw {:.1f}", eye.x, eye.y, eye.z, yaw);
            LoadingScreen progress(screenContext(), screenContext().translate("menu.working"));
            while (!world.viewMeshed() && timer.seconds() < 300.0 && !client_.window.shouldClose()) {
                world.frame(0.0);
                client_.jobs->drainMain(8.0);
                presentScreen(progress);
            }
            if (world.viewMeshed()) {
                Texture color, depth;
                color.create(kPanoramaSize, kPanoramaSize, 1);
                depth.createDepth(kPanoramaSize, kPanoramaSize);
                unsigned fbo = 0;
                glCreateFramebuffers(1, &fbo);
                glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, color.id(), 0);
                glNamedFramebufferTexture(fbo, GL_DEPTH_STENCIL_ATTACHMENT, depth.id(), 0);
                std::error_code ec;
                std::filesystem::create_directories(dir, ec);
                ok = glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
                for (int face = 0; ok && face < Panorama::kFaces; ++face) {
                    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
                    world.renderScenery(Panorama::faceCamera(face, eye, yaw), glm::ivec2(kPanoramaSize));
                    ok = captureFramebuffer(kPanoramaSize, kPanoramaSize)
                             .savePng(dir / std::format("panorama_{}.png", face));
                }
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glDeleteFramebuffers(1, &fbo);
            }
        }
    }
    client_.options.renderDistance = renderDistance;
    if (ok) log::info("Baked the title panorama in {:.1f} s", timer.seconds());
    else log::error("Could not bake the title panorama");
    return ok;
}

bool Game::loadRegistries() {
    const CliOptions& cli = client_.cli;
    std::string err;
    client_.registry = std::make_unique<BlockRegistry>();
    if (!client_.registry->build(client_.assets->blockModels(), client_.assets->blockAtlas(), cli.dataDir, &err))
        log::warn("block registry: {}", err); // problems are logged; the world still runs
    client_.lang = std::make_unique<Language>();
    if (!client_.lang->load(client_.assets->pack())) log::warn("no language file in the resource pack");
    client_.items = std::make_unique<ItemRegistry>();
    if (!client_.items->load(client_.assets->pack(), *client_.registry, cli.dataDir, &err)) log::warn("items: {}", err);
    log::info("Items: {}", client_.items->size());
    client_.creativeTabs = std::make_unique<CreativeTabs>();
    if (!client_.creativeTabs->load(cli.dataDir, *client_.items, &err)) log::warn("creative tabs: {}", err);
    client_.recipes = std::make_unique<RecipeBook>();
    if (!client_.recipes->load(cli.dataDir, *client_.items, &err)) {
        int shown = 0;
        for (size_t start = 0; start < err.size() && shown < 20; ++shown) {
            const size_t end = err.find('\n', start);
            log::warn("recipes: {}", err.substr(start, end - start));
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }
    log::info("Recipes: {}", client_.recipes->recipes().size());
    client_.loot = std::make_unique<BlockLoot>();
    if (!client_.loot->load(cli.dataDir / "loot" / "blocks.json", *client_.registry, *client_.items, &err))
        log::warn("block loot: {}", err.substr(0, err.find('\n')));
    client_.biomes = std::make_unique<BiomeRegistry>();
    if (!client_.biomes->load(cli.dataDir, cli.assetsDir, &err)) {
        log::error("biomes: {}", err);
        return false;
    }
    client_.entityTypes = std::make_unique<EntityTypes>();
    if (!client_.entityTypes->load(cli.dataDir / "entities.json", *client_.items, *client_.biomes, &err))
        log::warn("entities: {}", err);
    log::info("Mobs: {}", client_.entityTypes->all().size());
    return true;
}

void Game::shutdown() {
    // Menus hand their items back before the world saves.
    for (auto it = screens_.rbegin(); it != screens_.rend(); ++it) (*it)->removed();
    screens_.clear();
    world_.reset();
    gallery_.reset();
    panorama_.reset();
    client_.chat.reset();
    client_.gui.reset();
    // Jobs reference the registries and assets until the pool is joined.
    client_.jobs.reset();
    client_.creativeTabs.reset();
    client_.recipes.reset();
    client_.loot.reset();
    client_.entityTypes.reset();
    client_.items.reset();
    client_.biomes.reset();
    client_.registry.reset();
    client_.assets.reset();
    client_.window.destroy();
}

ScreenContext Game::screenContext() {
    ScreenContext ctx;
    ctx.lang = client_.lang.get();
    ctx.items = client_.items.get();
    ctx.host = this;
    ctx.font = &client_.gui->font().glyphs();
    return ctx;
}

void Game::setScreen(std::unique_ptr<Screen> screen) {
    screenChanges_.push_back({ScreenChange::Kind::Set, std::move(screen)});
}

void Game::pushScreen(std::unique_ptr<Screen> screen) {
    screenChanges_.push_back({ScreenChange::Kind::Push, std::move(screen)});
}

void Game::popScreen() { screenChanges_.push_back({ScreenChange::Kind::Pop, nullptr}); }

void Game::startWorld(const WorldParams& params) { pendingWorld_ = std::make_unique<WorldParams>(params); }

void Game::leaveWorld() {
    if (!world_ || leaveWorldPending_) return;
    // Vanilla disconnect: "Saving world" shows (one frame here) while the world saves.
    leaveWorldPending_ = true;
    leaveWorldFrame_ = frameCount_;
    setScreen(std::make_unique<LoadingScreen>(screenContext(), screenContext().translate("menu.savingLevel")));
}

void Game::quitGame() { client_.window.setShouldClose(true); }

void Game::optionsChanged() {
    // Window settings apply at once; the rest is read where it is used.
    client_.window.setVsync(client_.options.vsync);
    if (client_.window.fullscreen() != client_.options.fullscreen) client_.window.toggleFullscreen();
    std::string err;
    if (!client_.options.save(client_.cli.gameDir / "options.json", &err)) log::warn("{}", err);
}

std::filesystem::path Game::savesDir() const { return client_.cli.gameDir / "saves"; }

Difficulty Game::difficulty() const { return world_ ? world_->difficulty() : Difficulty::Normal; }

void Game::setDifficulty(Difficulty d) {
    if (world_) world_->setDifficulty(d);
}

void Game::sendChat(const std::string& line) {
    if (line.empty()) return;
    client_.chat->addSent(line);
    if (!world_) return;
    if (line[0] == '/') {
        world_->runCommand(line.substr(1), [this](const std::string& msg) { client_.chat->addMessage(msg, tickCount_); });
    } else {
        client_.chat->addMessage("<Player> " + line, tickCount_);
    }
}

void Game::leaveBed() {
    if (world_) world_->wakeUp();
}

void Game::respawn() {
    if (!world_) return;
    world_->respawnPlayer();
    setScreen(nullptr);
}

WorldLoadStatus Game::worldLoadStatus() const { return world_ ? world_->loadStatus() : WorldLoadStatus{}; }

void Game::applyScreenChanges() {
    // The top of the stack changes; covered screens stay alive for going back.
    std::vector<ScreenChange> changes;
    changes.swap(screenChanges_);
    for (ScreenChange& change : changes) {
        Screen* before = screen();
        switch (change.kind) {
        case ScreenChange::Kind::Set:
            for (auto it = screens_.rbegin(); it != screens_.rend(); ++it) (*it)->removed();
            screens_.clear();
            if (change.screen) screens_.push_back(std::move(change.screen));
            break;
        case ScreenChange::Kind::Push:
            if (change.screen) screens_.push_back(std::move(change.screen));
            break;
        case ScreenChange::Kind::Pop:
            if (!screens_.empty()) {
                screens_.back()->removed();
                screens_.pop_back();
            }
            break;
        }
        if (screen() && screen() != before) {
            const glm::ivec2 fb = client_.window.framebufferSize();
            screenSize_ = glm::vec2(fb) / float(client_.guiScale());
            screen()->init(screenSize_);
            client_.window.setCursorCaptured(false);
            lastGuiMouse_ = client_.guiMouse();
            guiButtonsDown_ = {};
        }
    }
}

void Game::applyPending() {
    const bool hadScreen = screen() != nullptr;
    applyScreenChanges();
    if (leaveWorldPending_ && frameCount_ > leaveWorldFrame_) {
        leaveWorldPending_ = false;
        for (auto it = screens_.rbegin(); it != screens_.rend(); ++it) (*it)->removed();
        screens_.clear();
        world_.reset(); // saves
        worldLoading_ = false;
        client_.chat->clear();
        client_.gui->forgetFileTextures(); // the world list reads the new icon
        openTitle();
    }
    if (pendingWorld_) {
        const WorldParams params = *pendingWorld_;
        pendingWorld_.reset();
        world_.reset();
        worldLoading_ = false;
        world_ = std::make_unique<WorldSession>(client_, params);
        if (!world_->init()) {
            log::error("Could not open world '{}'", params.name);
            world_.reset();
            if (!params.commandLine) openTitle();
        } else if (!params.commandLine) {
            // Vanilla LevelLoadingScreen until the spawn area is ready.
            worldLoading_ = true;
            setScreen(std::make_unique<LoadingScreen>(screenContext()));
        }
        meshedLogged_ = false;
    }
    applyScreenChanges();
    client_.screenOpen = screen() != nullptr;
    client_.chatOpen = dynamic_cast<ChatScreen*>(screen()) != nullptr;
    // Back in the game: the mouse is grabbed again (vanilla grabMouse when the screen closes).
    if (hadScreen && !screen() && world_) client_.window.setCursorCaptured(true);
}

std::unique_ptr<Screen> Game::makeScreen(const std::string& name) {
    const ScreenContext ctx = screenContext();
    if (name == "title") return std::make_unique<TitleScreen>(ctx);
    if (name == "worlds") return std::make_unique<SelectWorldScreen>(ctx);
    if (name == "create") return std::make_unique<CreateWorldScreen>(ctx);
    if (name == "pause") return std::make_unique<PauseScreen>(ctx);
    if (name == "sleep") return std::make_unique<SleepScreen>(ctx, *client_.chat);
    if (name == "sign" && world_ && world_->editingSign()) {
        auto sign = world_->editingSign();
        const StateId state = world_->level().getBlock(sign->pos);
        const Block& block = client_.registry->blockOf(state);
        std::string wood(shortId(block.id));
        for (const char* suffix : {"_wall_hanging_sign", "_hanging_sign", "_wall_sign", "_sign"})
            if (wood.ends_with(suffix)) {
                wood.resize(wood.size() - std::string_view(suffix).size());
                break;
            }
        return std::make_unique<SignEditScreen>(ctx, sign, world_->editingFront(), wood, block.className == "sign",
                                                [this] {
                                                    if (world_) world_->stopEditingSign();
                                                });
    }
    if (name == "container" && world_) {
        Menu* menu = world_->openMenu();
        if (auto* m = dynamic_cast<CraftingMenu*>(menu)) return std::make_unique<CraftingScreen>(ctx, *m, world_->player());
        if (auto* m = dynamic_cast<ChestMenu*>(menu)) return std::make_unique<ChestScreen>(ctx, *m, world_->player(), m->title);
        if (auto* m = dynamic_cast<FurnaceMenu*>(menu)) return std::make_unique<FurnaceScreen>(ctx, *m, world_->player());
    }
    if (name == "death" && world_) {
        const Player& p = world_->player();
        return std::make_unique<DeathScreen>(ctx, ctx.translate(std::string(p.deathMessageKey()), {"Player"}), p.score,
                                             world_->hardcore());
    }
    if (name == "options") return std::make_unique<OptionsScreen>(ctx);
    if (name == "video") return std::make_unique<VideoSettingsScreen>(ctx);
    if (name == "chat" || name == "command") return std::make_unique<ChatScreen>(ctx, *client_.chat, name == "command" ? "/" : "");
    if (name == "inventory" && world_) {
        Player& player = world_->player();
        if (player.mode() == GameMode::Spectator) return nullptr;
        if (player.mode() == GameMode::Creative)
            return std::make_unique<CreativeScreen>(screenContext(), player, world_->inventoryMenu(), *client_.creativeTabs);
        return std::make_unique<InventoryScreen>(screenContext(), world_->inventoryMenu(), player);
    }
    return nullptr;
}

void Game::openHarnessScreen() {
    const std::string& name = client_.cli.screen;
    if (name.empty() || name == "title") return;
    // In a world it replaces the game view; at the title it opens over the title screen.
    if (auto s = makeScreen(name)) {
        if (world_) setScreen(std::move(s));
        else pushScreen(std::move(s));
    }
}

int Game::run() {
    if (!init()) return 1;
    if (toolOnly_) return exitCode_;

    double last = Window::time();
    double accumulator = 0.0;
    runTimer_.reset();
    while (!client_.window.shouldClose()) {
        client_.window.pollEvents();
        handleInput();
        applyPending();

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
        applyPending();

        // Vanilla PanoramaRenderer: 0.1 degrees per tick of real time.
        panoramaSpin_ = std::fmod(panoramaSpin_ + float(frameDt / kTickSeconds) * 0.1f, 360.0f);
        if (world_) {
            world_->frame(frameDt);
            if (worldLoading_ && world_->viewReady()) {
                worldLoading_ = false;
                setScreen(nullptr);
                applyPending();
            }
            if (!meshedLogged_ && world_->viewMeshed()) {
                meshedLogged_ = true;
                log::info("View meshed after {:.2f} s", runTimer_.seconds());
            }
            if (!harnessStarted_ && client_.cli.worldRequested && world_->viewReady()) {
                harnessStarted_ = true;
                client_.harnessRunning = true;
                harnessStartTick_ = tickCount_;
                for (const auto& cmd : client_.cli.exec)
                    world_->runCommand(cmd, [](const std::string& msg) { log::info("[command] {}", msg); });
                openHarnessScreen();
                applyPending();
            }
        } else if (gallery_) {
            updateGallery(frameDt);
        }
        client_.jobs->drainMain(6.0);
        render(float(accumulator / kTickSeconds));
        ++frameCount_;
        afterFrame();
        client_.window.swapBuffers();
        limitFramerate();
        updateTitle();
    }
    log::info("Ran {} frames in {:.2f} s ({:.0f} fps avg)", frameCount_, runTimer_.seconds(),
              double(frameCount_) / std::max(runTimer_.seconds(), 1e-6));
    return exitCode_;
}

void Game::handleInput() {
    const Input& in = client_.window.input();
    if (in.keyPressed(GLFW_KEY_F11)) client_.window.toggleFullscreen();
    if (in.keyPressed(GLFW_KEY_F2)) screenshotRequested_ = true;
    if (screen()) {
        handleScreenInput();
        return;
    }
    if (!world_) {
        if (in.keyPressed(GLFW_KEY_ESCAPE)) client_.window.setShouldClose(true);
        if (in.mousePressed(GLFW_MOUSE_BUTTON_LEFT)) client_.window.setCursorCaptured(true);
        return;
    }
    const std::string request = world_->handleInput();
    if (!request.empty())
        if (auto s = makeScreen(request)) setScreen(std::move(s));
}

void Game::handleScreenInput() {
    Screen* s = screen();
    const Input& in = client_.window.input();
    const glm::vec2 mouse = client_.guiMouse();
    for (const Input::KeyEvent& e : in.keyEvents()) {
        if (e.action == GLFW_RELEASE) continue;
        if (!s->keyPressed(e.key, e.scancode, e.mods) && e.key == GLFW_KEY_ESCAPE && e.action == GLFW_PRESS &&
            s->closeOnEscape())
            s->close();
    }
    for (char32_t c : in.typedText()) s->charTyped(c);
    for (int b = 0; b < 3; ++b)
        if (in.mousePressed(b)) {
            s->mouseClicked(mouse, b, in.mods());
            guiButtonsDown_[size_t(b)] = true;
        }
    if (mouse != lastGuiMouse_)
        for (int b = 0; b < 3; ++b)
            if (guiButtonsDown_[size_t(b)] && in.mouseDown(b)) s->mouseDragged(mouse, b, mouse - lastGuiMouse_);
    for (int b = 0; b < 3; ++b)
        if (in.mouseReleased(b) && guiButtonsDown_[size_t(b)]) {
            s->mouseReleased(mouse, b, in.mods());
            guiButtonsDown_[size_t(b)] = false;
        }
    if (in.scrollY() != 0.0) s->mouseScrolled(mouse, in.scrollY());
    lastGuiMouse_ = mouse;
    if (s->closing()) popScreen();
}

void Game::tick() {
    ++tickCount_;
    client_.chatTicks = tickCount_;
    client_.assets->tick();
    // Single player pauses under pause screens (vanilla Minecraft.pause).
    const bool paused = screen() && screen()->isPauseScreen();
    if (world_ && !paused) {
        world_->tick();
        if (const std::string request = world_->takePendingScreen(); request == "close") {
            if (dynamic_cast<ContainerScreen*>(screen()) || dynamic_cast<SleepScreen*>(screen()) ||
                dynamic_cast<SignEditScreen*>(screen()))
                setScreen(nullptr);
        } else if (!request.empty()) {
            if (auto s = makeScreen(request)) setScreen(std::move(s));
        }
    }
    if (screen()) screen()->tick();
}

void Game::render(float partialTick) {
    const glm::ivec2 fb = client_.window.framebufferSize();
    if (world_ && !worldLoading_) {
        world_->fps = fps_;
        world_->render(fb, partialTick, [&] { renderScreen(partialTick); });
    } else {
        renderWithoutWorld(fb, partialTick);
    }
}

void Game::renderScreen(float partialTick) {
    Screen* s = screen();
    if (!s) return;
    GuiGraphics& gui = *client_.gui;
    // Re-lay out after a resize or a GUI scale change.
    if (gui.size() != screenSize_) {
        screenSize_ = gui.size();
        s->init(screenSize_);
    }
    s->render(gui, client_.guiMouse(), partialTick);
}

void Game::renderWithoutWorld(glm::ivec2 fb, float partialTick) {
    glViewport(0, 0, fb.x, fb.y);
    if (gallery_) {
        glClearColor(120.0f / 255.0f, 167.0f / 255.0f, 1.0f, 1.0f); // vanilla daytime sky blue
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        gallery_->render(galleryCamera_, fb);
        return;
    }
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // Vanilla Screen.renderPanorama: the cube, then the overlay stretched over it.
    if (panorama_) panorama_->render(fb, panoramaSpin_, 10.0f);
    GuiGraphics& gui = *client_.gui;
    gui.begin(fb, client_.guiScale());
    const glm::vec2 size = gui.size();
    gui.texture("minecraft:gui/title/background/panorama_overlay", 0, 0, 0, 0, size.x, size.y, int(size.x), int(size.y));
    renderScreen(partialTick);
    gui.end();
}

void Game::updateGallery(double dt) {
    const Input& in = client_.window.input();
    Camera& camera = galleryCamera_;
    if (client_.window.cursorCaptured()) {
        const glm::dvec2 d = in.mouseDelta();
        const double k = client_.options.mouseDegreesPerCount();
        camera.yaw += float(d.x * k);
        camera.pitch = std::clamp(camera.pitch + float(d.y * k), -90.0f, 90.0f);
    }
    glm::vec3 move(0.0f);
    const glm::vec3 fwd = glm::normalize(glm::vec3(camera.forward().x, 0.0f, camera.forward().z));
    if (in.keyDown(GLFW_KEY_W)) move += fwd;
    if (in.keyDown(GLFW_KEY_S)) move -= fwd;
    if (in.keyDown(GLFW_KEY_D)) move += camera.right();
    if (in.keyDown(GLFW_KEY_A)) move -= camera.right();
    if (in.keyDown(GLFW_KEY_SPACE)) move.y += 1.0f;
    if (in.keyDown(GLFW_KEY_LEFT_SHIFT)) move.y -= 1.0f;
    if (glm::length(move) > 0.0f) {
        const double speed = in.keyDown(GLFW_KEY_LEFT_CONTROL) ? 40.0 : 10.0;
        camera.position += glm::dvec3(glm::normalize(move)) * speed * dt;
    }
}

void Game::afterFrame() {
    const CliOptions& cli = client_.cli;
    const glm::ivec2 fb = client_.window.framebufferSize();
    if (screenshotRequested_) {
        screenshotRequested_ = false;
        saveScreenshot(nextScreenshotPath(cli.gameDir / "screenshots"), fb.x, fb.y);
    }
    if (world_ && world_->assertionFailed()) exitCode_ = 1;
    if (harnessDone_) return;
    const bool harness = !cli.screenshot.empty() || cli.exitAfter;
    if (!harness || frameCount_ < std::max(cli.frames, 1) || tickCount_ < cli.ticks) return;
    if (world_ && cli.worldRequested && (!cli.exec.empty() || cli.waitChunks || !cli.screen.empty())) {
        // Here --ticks counts from when the world was ready and the --exec commands ran, so
        // a slow start (sanitizers, llvmpipe) doesn't use them up.
        if (!harnessStarted_ || tickCount_ - harnessStartTick_ < cli.ticks) return;
        if (cli.waitChunks && !world_->viewMeshed()) return;
    }
    harnessDone_ = true;
    if (!cli.screenshot.empty()) saveScreenshot(cli.screenshot, fb.x, fb.y);
    if (cli.exitAfter) client_.window.setShouldClose(true);
}

// Vanilla getFramerateLimit: the option in a world, 60 in menus; vsync paces itself.
void Game::limitFramerate() {
    const Options& o = client_.options;
    const int limit = world_ ? (o.unlimitedFps() ? 0 : o.maxFps) : 60;
    const double now = Window::time();
    if (limit > 0 && !o.vsync && client_.cli.screenshot.empty()) {
        const double frame = 1.0 / double(limit);
        if (now - lastFrameEnd_ < frame) std::this_thread::sleep_for(std::chrono::duration<double>(frame - (now - lastFrameEnd_)));
        lastFrameEnd_ = std::max(lastFrameEnd_ + frame, Window::time() - frame);
    } else {
        lastFrameEnd_ = now;
    }
}

void Game::updateTitle() {
    ++framesSinceTitle_;
    if (titleTimer_.seconds() < 1.0) return;
    fps_ = framesSinceTitle_ / titleTimer_.seconds();
    framesSinceTitle_ = 0;
    titleTimer_.reset();
    std::string title = std::format("Minecraft  |  {:.0f} fps", fps_);
    if (world_) title += "  |  " + world_->statusLine();
    client_.window.setTitle(title);
}

} // namespace mc
