// CreativeScreen.h -- the creative inventory (vanilla CreativeModeInventoryScreen):
// item tabs above and below the window, a scrollable 9x5 grid of each tab's
// items, the search tab with its text box, and the survival inventory tab with
// the destroy-item slot. Clicking tab items gives copies (vanilla semantics).
#pragma once

#include <string>
#include <vector>

#include "game/CreativeTabs.h"
#include "ui/ContainerScreen.h"

namespace mc {

// Holds the menu so it exists before ContainerScreen (a base) refers to it.
struct CreativeMenuHolder {
    CreativeMenu creativeMenu;
};

class CreativeScreen : private CreativeMenuHolder, public ContainerScreen {
public:
    CreativeScreen(ScreenContext ctx, Player& player, InventoryMenu& inventoryMenu, const CreativeTabs& tabs);

    void init(glm::vec2 size) override;
    void render(GuiGraphics& g, glm::vec2 mouse, float partialTick) override;
    bool mouseClicked(glm::vec2 mouse, int button, int mods) override;
    bool mouseReleased(glm::vec2 mouse, int button, int mods) override;
    bool mouseDragged(glm::vec2 mouse, int button, glm::vec2 delta) override;
    bool mouseScrolled(glm::vec2 mouse, double amount) override;
    bool keyPressed(int key, int scancode, int mods) override;
    bool charTyped(char32_t c) override;
    void tick() override { ++ticks_; }
    void removed() override;

    // The last selected tab survives closing the screen (vanilla selectedTab is static).
    static inline int lastTab = 0;

protected:
    void renderBg(GuiGraphics& g, glm::vec2 mouse, float partialTick) override;
    void renderLabels(GuiGraphics& g, glm::vec2 mouse) override;
    void slotClicked(Slot* slot, int slotId, int button, ClickType type) override;
    bool clickedOutside(glm::vec2 mouse) const override;

private:
    struct Tab {
        const CreativeTab* data = nullptr; // null for search and inventory
        enum class Kind : uint8_t { Items, Search, Inventory } kind = Kind::Items;
        std::string name;
        const Item* icon = nullptr;
        bool top = true;
        int column = 0;
        bool alignedRight = false;
    };
    void selectTab(int index);
    void refreshSearch();
    float tabX(const Tab& t) const;
    float tabY(const Tab& t) const;
    bool overTab(const Tab& t, glm::vec2 mouse) const;
    bool insideScrollbar(glm::vec2 mouse) const;
    void renderTab(GuiGraphics& g, const Tab& t, bool selected);

    InventoryMenu& inventoryMenu_;
    const CreativeTabs& tabs_;
    std::vector<Tab> tabList_;
    int selected_ = 0;
    std::u32string search_;
    bool scrolling_ = false;
    bool clickedOutside_ = false;
    int ticks_ = 0;
};

} // namespace mc
