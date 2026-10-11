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
#include "ui/Font.h"
#include "ui/GuiBatch.h"
#include "world/light/LightEngine.h"
#include "world/block/BlockRegistry.h"
#include "world/biome/Biome.h"
#include "world/gen/ChunkGenerator.h"
#include "world/gen/OverworldGenerator.h"
#include "world/gen/feature/BiomeDecorator.h"
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

    gui_ = std::make_unique<GuiBatch>();
    font_ = std::make_unique<Font>();
    if (!gui_->init() || !font_->load(assets_->pack())) return false;
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

    biomes_ = std::make_unique<BiomeRegistry>();
    if (!biomes_->load(opts_.dataDir, opts_.assetsDir, &err)) {
        log::error("biomes: {}", err);
        return false;
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
    level_->chunks().setLightEngine(std::make_shared<LightEngine>(*registry_));
    if (opts_.time) level_->setDayTime(*opts_.time);
    level_->daylightCycle = !opts_.freezeTime;
    worldRenderer_ = std::make_unique<WorldRenderer>(*assets_, *registry_, opts_.worldType == "default" ? biomes_.get() : nullptr);
    if (!worldRenderer_->init()) return false;
    worldRenderer_->mesherOptions().smoothLighting = opts_.smoothLighting;

    camera_.position = opts_.pos.value_or(generator->spawnPoint() + glm::dvec3(0.0, overworld_ ? 12.0 : 1.62, 0.0));
    if (std::isnan(camera_.position.y)) { // --pos X,~,Z
        const int x = int(std::floor(camera_.position.x)), z = int(std::floor(camera_.position.z));
        camera_.position.y = (overworld_ ? overworld_->terrainTop(x, z) + 1 : generator->spawnPoint().y) + 12.0;
    }
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
    overworld_.reset();
    font_.reset();
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
    if (in.keyPressed(GLFW_KEY_F3)) showDebug_ = !showDebug_;
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
    if (worldRenderer_) worldRenderer_->tick();
}

void Game::render(float partialTick) {
    const glm::ivec2 fb = window_.framebufferSize();
    if (worldRenderer_) {
        worldRenderer_->render(camera_, fb, opts_.renderDistance, *level_, partialTick);
    } else {
        glViewport(0, 0, fb.x, fb.y);
        glClearColor(120.0f / 255.0f, 167.0f / 255.0f, 1.0f, 1.0f); // vanilla daytime sky blue
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (gallery_) gallery_->render(camera_, fb);
    }
    if (showDebug_) renderDebugOverlay(fb);
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
    } else {
        lines.push_back(std::format("{:.0f} fps", fps_));
        lines.push_back(std::format("XYZ: {:.3f} / {:.5f} / {:.3f}", p.x, p.y, p.z));
    }
    gui_->begin(fb, autoGuiScale(fb));
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].empty()) continue;
        const float y = 2.0f + 9.0f * float(i);
        gui_->fill(1, y - 1, 2.0f + float(font_->width(lines[i])) + 1.0f, y + 8, 0x90505050);
        font_->draw(*gui_, lines[i], 2, y, 0xFFE0E0E0, false);
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
