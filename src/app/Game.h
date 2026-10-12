// Game.h -- runs the client: the fixed-tick main loop, the screen stack and the
// open world (if any). Screens act on it through ScreenHost.
#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "app/Client.h"
#include "core/Timer.h"
#include "game/WorldParams.h"
#include "gfx/Camera.h"
#include "render/Panorama.h"
#include "ui/Screen.h"

namespace mc {

class Gallery;
class WorldSession;

class Game final : public ScreenHost {
public:
    static constexpr double kTickSeconds = 0.05; // 20 ticks per second

    explicit Game(CliOptions options);
    ~Game() override;

    // Returns the process exit code.
    int run();

    // ScreenHost
    void setScreen(std::unique_ptr<Screen> screen) override;
    void pushScreen(std::unique_ptr<Screen> screen) override;
    void popScreen() override;
    void startWorld(const WorldParams& params) override;
    void leaveWorld() override;
    void quitGame() override;
    bool inWorld() const override { return world_ != nullptr && !worldLoading_; }
    Options& options() override { return client_.options; }
    void optionsChanged() override;
    std::filesystem::path savesDir() const override;
    const std::string& splash() const override { return splash_; }
    WorldLoadStatus worldLoadStatus() const override;
    Difficulty difficulty() const override;
    void setDifficulty(Difficulty d) override;
    void sendChat(const std::string& line) override;
    void respawn() override;
    void leaveBed() override;

private:
    bool init();
    bool loadRegistries();
    void shutdown();
    void handleInput();
    void handleScreenInput();
    void applyPending(); // screen and world changes requested during callbacks
    void applyScreenChanges();
    void openTitle();
    void loadSplash();
    // The title panorama from <gameDir>/panorama, baked first if it is missing.
    void loadPanorama();
    // Renders the six panorama faces of a fixed world into `dir` (vanilla's own are
    // pictures; the pack's are 1x1 placeholders).
    bool bakePanorama(const std::filesystem::path& dir);
    // One frame of `screen` over the panorama, for work that blocks the loop.
    void presentScreen(Screen& screen);
    void tick();
    void render(float partialTick);
    void renderWithoutWorld(glm::ivec2 fb, float partialTick);
    void renderScreen(float partialTick);
    void afterFrame(); // harness: screenshot / exit when the run conditions are met
    void updateTitle();
    void limitFramerate();
    void updateGallery(double dt);
    void openHarnessScreen(); // --screen
    std::unique_ptr<Screen> makeScreen(const std::string& name);
    ScreenContext screenContext();

    Client client_;
    std::unique_ptr<WorldSession> world_;
    // Open screens, the top one active; changes queue up while screens run.
    std::vector<std::unique_ptr<Screen>> screens_;
    struct ScreenChange {
        enum class Kind : uint8_t { Set, Push, Pop } kind;
        std::unique_ptr<Screen> screen;
    };
    std::vector<ScreenChange> screenChanges_;
    Screen* screen() const { return screens_.empty() ? nullptr : screens_.back().get(); }
    bool leaveWorldPending_ = false;
    long long leaveWorldFrame_ = 0; // the "Saving world" screen is drawn once before the save
    bool worldLoading_ = false;     // a world opened from the menus waits behind the loading screen
    std::unique_ptr<Panorama> panorama_;
    float panoramaSpin_ = 0.0f; // degrees, 0.1 per tick of real time (vanilla PanoramaRenderer)
    std::unique_ptr<WorldParams> pendingWorld_;
    std::unique_ptr<Gallery> gallery_;
    Camera galleryCamera_;
    glm::vec2 lastGuiMouse_{-1.0f};
    std::array<bool, 3> guiButtonsDown_{};
    glm::vec2 screenSize_{0.0f};
    double fps_ = 0.0;
    std::string splash_;
    double lastFrameEnd_ = 0.0;

    long long frameCount_ = 0;
    long long tickCount_ = 0;
    bool harnessDone_ = false;
    bool harnessStarted_ = false; // --exec and --screen ran
    long long harnessStartTick_ = 0; // when, so --ticks counts from there in a world
    bool screenshotRequested_ = false;
    bool toolOnly_ = false; // a --dump-* tool ran inside init()
    int exitCode_ = 0;
    Stopwatch titleTimer_;
    int framesSinceTitle_ = 0;
    Stopwatch runTimer_;
    bool meshedLogged_ = false;
};

} // namespace mc
