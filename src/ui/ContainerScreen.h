// ContainerScreen.h -- a menu's window (vanilla AbstractContainerScreen): its
// background, slots, the hovered-slot highlight, labels, the item on the cursor
// and tooltips; mouse and key input become menu clicks, with vanilla's drag
// splitting, double clicks, number-key swaps and throwing.
#pragma once

#include <string>
#include <vector>

#include "game/Menu.h"
#include "ui/Screen.h"

namespace mc {

class Player;

class ContainerScreen : public Screen {
public:
    ContainerScreen(ScreenContext ctx, Menu& menu, Player& player, std::string title);

    void init(glm::vec2 size) override;
    void render(GuiGraphics& g, glm::vec2 mouse, float partialTick) override;
    bool mouseClicked(glm::vec2 mouse, int button, int mods) override;
    bool mouseReleased(glm::vec2 mouse, int button, int mods) override;
    bool mouseDragged(glm::vec2 mouse, int button, glm::vec2 delta) override;
    bool keyPressed(int key, int scancode, int mods) override;
    bool isPauseScreen() const override { return false; }
    void removed() override;

protected:
    virtual void renderBg(GuiGraphics& g, glm::vec2 mouse, float partialTick) = 0;
    virtual void renderLabels(GuiGraphics& g, glm::vec2 mouse);
    // Turns a slot click into a menu click (screens with special slots override it).
    virtual void slotClicked(Slot* slot, int slotId, int button, ClickType type);
    virtual bool clickedOutside(glm::vec2 mouse) const;
    void renderSlot(GuiGraphics& g, Slot& slot);
    Slot* hoveredSlot(glm::vec2 mouse);
    // The empty-slot icon (armor outlines, the shield in the off hand), or null.
    virtual const char* emptySlotIcon(const Slot& slot) const;

    Menu& menu_;
    Player& player_;
    std::string title_;
    int imageWidth_ = 176, imageHeight_ = 166;
    float leftPos_ = 0, topPos_ = 0;
    float titleX_ = 8, titleY_ = 6;
    float inventoryLabelX_ = 8, inventoryLabelY_ = 72;
    bool showInventoryLabel_ = true;

private:
    void recalculateQuickCraftRemaining();

    Slot* hovered_ = nullptr;
    bool quickCrafting_ = false;
    int quickCraftButton_ = 0, quickCraftType_ = 0, quickCraftRemaining_ = 0;
    std::vector<Slot*> quickCraftSlots_;
    bool skipNextRelease_ = false;
    bool doubleClick_ = false;
    Slot* lastClickSlot_ = nullptr;
    double lastClickTime_ = -1.0;
    int lastClickButton_ = -1;
    ItemStack lastQuickMoved_;
};

} // namespace mc
