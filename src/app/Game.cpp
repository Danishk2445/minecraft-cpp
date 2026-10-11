#include "app/Game.h"

#include <algorithm>

#include <GLFW/glfw3.h>

#include "core/Log.h"
#include "gfx/GL.h"
#include "gfx/Screenshot.h"
#include "gfx/Shader.h"
#include "render/ClientAssets.h"
#include "render/Gallery.h"

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

    gallery_ = std::make_unique<Gallery>();
    if (!gallery_->init(*assets_)) return false;
    camera_.position = opts_.pos.value_or(gallery_->startPosition());
    const glm::vec2 look = opts_.look.value_or(glm::vec2(-45.0f, 35.0f));
    camera_.yaw = look.x;
    camera_.pitch = look.y;
    return true;
}

void Game::shutdown() {
    gallery_.reset();
    assets_.reset();
    jobs_.reset();
    window_.destroy();
}

int Game::run() {
    if (!init()) return 1;
    if (toolOnly_) return exitCode_;

    double last = Window::time();
    double accumulator = 0.0;
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
        jobs_->drainMain(4.0);
        render(float(accumulator / kTickSeconds));
        ++frameCount_;
        afterFrame();
        window_.swapBuffers();
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

void Game::tick() {
    ++tickCount_;
    assets_->tick();
}

void Game::render(float partialTick) {
    const glm::ivec2 fb = window_.framebufferSize();
    glViewport(0, 0, fb.x, fb.y);
    // Vanilla's daytime sky blue (#78A7FF).
    glClearColor(120.0f / 255.0f, 167.0f / 255.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (gallery_) gallery_->render(camera_, fb);
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
    harnessDone_ = true;
    if (!opts_.screenshot.empty()) saveScreenshot(opts_.screenshot, fb.x, fb.y);
    if (opts_.exitAfter) window_.setShouldClose(true);
}

} // namespace mc
