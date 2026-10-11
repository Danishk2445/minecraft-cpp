// Screen.h -- a GUI screen over the game (vanilla Screen): laid out and driven in
// GUI pixels, drawn after the HUD. The game routes input here while it is open.
#pragma once

#include <string>

#include <glm/glm.hpp>

#include "game/Inventory.h"

namespace mc {

class GuiGraphics;
class ItemRegistry;
class Language;

// Text and names screens look up.
struct ScreenContext {
    const Language* lang = nullptr;
    const ItemRegistry* items = nullptr;

    std::string translate(const std::string& key) const;
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
