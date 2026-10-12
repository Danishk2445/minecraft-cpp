// Screen.h -- a GUI screen over the game (vanilla Screen): laid out and driven in
// GUI pixels, drawn after the HUD. The game routes input here while it is open.
#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "game/Inventory.h"
#include "game/WorldParams.h"

namespace mc {

class FontSet;
class GuiGraphics;
class ItemRegistry;
class Language;
class Screen;
struct Options;

// The loading screen's view of a world being opened (vanilla StoringChunkProgressListener).
struct WorldLoadStatus {
    float progress = 0.0f; // 0..1
    int diameter = 0;      // chunks across the square around the spawn
    // Per chunk, rows of increasing z: 0 nothing yet, 1 terrain, 2 decorated, 3 final,
    // 4 lit, 5 ready (lit with lit neighbors).
    std::vector<uint8_t> stages;
};

// What screens can ask of the game (vanilla Minecraft.setScreen and friends).
class ScreenHost {
public:
    virtual ~ScreenHost() = default;
    // The screen stack (applied after the current input or tick): replace everything
    // (null: back to the game), open on top of the current screen, or go back to it.
    virtual void setScreen(std::unique_ptr<Screen> screen) = 0;
    virtual void pushScreen(std::unique_ptr<Screen> screen) = 0;
    virtual void popScreen() = 0;
    virtual void startWorld(const WorldParams& params) = 0;
    // Saves and closes the world, back to the title screen.
    virtual void leaveWorld() = 0;
    virtual void quitGame() = 0;
    virtual bool inWorld() const = 0;
    virtual Options& options() = 0;
    // Applies changed options (window, renderer) and saves them.
    virtual void optionsChanged() = 0;
    virtual std::filesystem::path savesDir() const = 0;
    // This session's title splash (texts/splashes.txt).
    virtual const std::string& splash() const = 0;
    // How far the spawn area of a world being opened is.
    virtual WorldLoadStatus worldLoadStatus() const = 0;
    // The open world's difficulty (Normal without a world).
    virtual Difficulty difficulty() const = 0;
    virtual void setDifficulty(Difficulty d) = 0;
    // Chat input: a message or a /command.
    virtual void sendChat(const std::string& line) = 0;
};

// Text and names screens look up, and the host they act on.
struct ScreenContext {
    const Language* lang = nullptr;
    const ItemRegistry* items = nullptr;
    ScreenHost* host = nullptr;
    const FontSet* font = nullptr; // text metrics for laying out (wrapping, widths)

    std::string translate(const std::string& key) const;
    // The translation with its %s (and %1$s, %2$s) slots filled in order.
    std::string translate(const std::string& key, const std::vector<std::string>& args) const;
    // The item's name with its rarity color (vanilla getHoverName styled by rarity).
    std::string itemName(const ItemStack& stack) const;
};

class Screen {
public:
    // GLFW modifier bits.
    static constexpr int kShift = 1, kControl = 2, kAlt = 4;

    explicit Screen(ScreenContext ctx) : ctx_(ctx) {}
    virtual ~Screen() = default;

    // Lays the screen out for a GUI `size` pixels big (on open and on resize).
    virtual void init(glm::vec2 size) { size_ = size; }
    virtual void render(GuiGraphics& g, glm::vec2 mouse, float partialTick) = 0;
    virtual void tick() {}
    virtual bool mouseClicked(glm::vec2 mouse, int button, int mods) { return false; }
    virtual bool mouseReleased(glm::vec2 mouse, int button, int mods) { return false; }
    virtual bool mouseDragged(glm::vec2 mouse, int button, glm::vec2 delta) { return false; }
    virtual bool mouseScrolled(glm::vec2 mouse, double amount) { return false; }
    // Keys the screen doesn't take: Esc closes it.
    virtual bool keyPressed(int key, int scancode, int mods) { return false; }
    virtual bool charTyped(char32_t c) { return false; }
    // Single player pauses the game under pause screens (not under inventories).
    virtual bool isPauseScreen() const { return true; }
    // Esc closes it (not the title screen).
    virtual bool closeOnEscape() const { return true; }
    // Called once when the screen closes.
    virtual void removed() {}

    void close() { closing_ = true; }
    bool closing() const { return closing_; }

protected:
    ScreenContext ctx_;
    glm::vec2 size_{0.0f};
    bool closing_ = false;
};

} // namespace mc
