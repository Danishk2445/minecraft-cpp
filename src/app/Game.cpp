#include "app/Game.h"

#include <algorithm>

#include <GLFW/glfw3.h>

#include "core/Log.h"
#include "gfx/GL.h"
#include "gfx/Screenshot.h"
#include "gfx/Shader.h"

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
    return true;
}

void Game::shutdown() {
    jobs_.reset();
    window_.destroy();
}

int Game::run() {
    if (!init()) return 1;

    double last = Window::time();
    double accumulator = 0.0;
    while (!window_.shouldClose()) {
        window_.pollEvents();
        handleGlobalKeys();

        const double now = Window::time();
        accumulator += std::min(now - last, 0.25); // after a stall, skip ahead instead of catching up
        last = now;
        int ticksThisFrame = 0;
        while (accumulator >= kTickSeconds && ticksThisFrame < 10) {
            tick();
            accumulator -= kTickSeconds;
            ++ticksThisFrame;
        }

        jobs_->drainMain(4.0);
        render(float(accumulator / kTickSeconds));
        ++frameCount_;
        afterFrame();
        window_.swapBuffers();
    }
    return 0;
}

void Game::handleGlobalKeys() {
    const Input& in = window_.input();
    if (in.keyPressed(GLFW_KEY_F11)) window_.toggleFullscreen();
    if (in.keyPressed(GLFW_KEY_F2)) screenshotRequested_ = true;
    if (in.keyPressed(GLFW_KEY_ESCAPE)) window_.setShouldClose(true);
}

void Game::tick() { ++tickCount_; }

void Game::render(float partialTick) {
    const glm::ivec2 fb = window_.framebufferSize();
    glViewport(0, 0, fb.x, fb.y);
    // Vanilla's daytime sky blue (#78A7FF).
    glClearColor(120.0f / 255.0f, 167.0f / 255.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
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
