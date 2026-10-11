#include "app/Game.h"

#include <algorithm>
#include <cmath>
#include <format>

#include <GLFW/glfw3.h>

#include "core/Log.h"
#include "gfx/GL.h"
#include "gfx/Screenshot.h"
#include "gfx/Shader.h"
#include "render/ClientAssets.h"
#include "render/Gallery.h"
#include "render/WorldRenderer.h"
#include "world/block/BlockRegistry.h"
#include "world/gen/ChunkGenerator.h"
#include "world/level/Level.h"

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

    std::shared_ptr<const ChunkGenerator> generator;
    if (opts_.worldType == "flat") generator = std::make_shared<FlatGenerator>(*registry_);
    else generator = std::make_shared<DebugGenerator>(*registry_);
    level_ = std::make_unique<Level>(*registry_, generator, *jobs_, opts_.seed);
    worldRenderer_ = std::make_unique<WorldRenderer>(*assets_, *registry_);
    if (!worldRenderer_->init()) return false;

    camera_.position = opts_.pos.value_or(generator->spawnPoint() + glm::dvec3(0.0, 1.62, 0.0));
    const glm::vec2 look = opts_.look.value_or(glm::vec2(-45.0f, 25.0f));
    camera_.yaw = look.x;
    camera_.pitch = look.y;
    camera_.farPlane = float(opts_.renderDistance * 16 + 64) * 1.5f;
    log::info("World: type {}, seed {}, render distance {}", generator->type(), opts_.seed, opts_.renderDistance);
    return true;
}

void Game::shutdown() {
    // Order matters: jobs reference the registry and assets until the pool is joined.
    worldRenderer_.reset();
    level_.reset();
    gallery_.reset();
    jobs_.reset();
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
    if (in.keyPressed(GLFW_KEY_ESCAPE)) {
        if (window_.cursorCaptured()) window_.setCursorCaptured(false);
        else window_.setShouldClose(true);
    }
    if (in.mousePressed(GLFW_MOUSE_BUTTON_LEFT) && !window_.cursorCaptured()) window_.setCursorCaptured(true);
}

void Game::updateCamera(double dt) {
    const Input& in = window_.input();
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
    }
}

void Game::runCommand(const std::string& line) {
    CommandSource src;
    src.level = level_.get();
    src.position = camera_.position;
    src.rotation = {camera_.yaw, camera_.pitch};
    src.feedback = [](const std::string& msg) { log::info("[command] {}", msg); };
    src.teleport = [this](const glm::dvec3& p, std::optional<glm::vec2> rot) {
        camera_.position = p;
        if (rot) {
            camera_.yaw = rot->x;
            camera_.pitch = rot->y;
        }
    };
    std::string err;
    if (!commands_.execute(src, line, &err)) log::warn("command '{}' failed: {}", line, err);
}

void Game::tick() {
    ++tickCount_;
    assets_->tick();
    if (level_) level_->tick();
}

void Game::render(float partialTick) {
    const glm::ivec2 fb = window_.framebufferSize();
    glViewport(0, 0, fb.x, fb.y);
    // Vanilla's daytime sky blue (#78A7FF).
    glClearColor(120.0f / 255.0f, 167.0f / 255.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (gallery_) gallery_->render(camera_, fb);
    if (worldRenderer_) worldRenderer_->render(camera_, fb, opts_.renderDistance);
    (void)partialTick;
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
