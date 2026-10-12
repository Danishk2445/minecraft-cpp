// GuiGraphics.h -- 2D drawing in GUI pixels: fills and gradients, GUI atlas
// sprites with their scaling (stretch, tile, nine-slice), standalone textures
// (container backgrounds) and text. Vanilla GuiGraphics.
#pragma once

#include <filesystem>
#include <memory>
#include <unordered_map>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "render/ItemRenderer.h"
#include "render/MenuBlur.h"
#include "ui/Font.h"
#include "ui/GuiBatch.h"

namespace mc {

class Atlas;
class ClientAssets;
class Player;
struct SpriteInfo;

class GuiGraphics {
public:
    bool init(const ClientAssets& assets);

    // Starts a frame (or a GUI pass on top of the world) at `scale` framebuffer pixels per GUI pixel.
    void begin(glm::ivec2 framebuffer, int scale);
    void end();
    void flush() { batch_.flush(); }
    // Blurs everything drawn so far, the GUI included (vanilla renderBlurredBackground);
    // `radius` is the menu background blurriness option.
    void blurBehind(int radius);
    glm::vec2 size() const { return batch_.size(); }
    int scale() const { return batch_.scale(); }
    GuiBatch& batch() { return batch_; }
    const Font& font() const { return font_; }
    const ClientAssets& assets() const { return *assets_; }

    void fill(float x0, float y0, float x1, float y1, uint32_t argb) { batch_.fill(x0, y0, x1, y1, argb); }
    // Top-to-bottom gradient (vanilla fillGradient).
    void fillGradient(float x0, float y0, float x1, float y1, uint32_t top, uint32_t bottom);
    // A GUI sprite ("minecraft:hud/hotbar" or "hud/hotbar") scaled per its mcmeta.
    void sprite(std::string_view id, float x, float y, float w, float h, uint32_t argb = 0xFFFFFFFF);
    // Part of a sprite whose nominal size is texW x texH (vanilla blitSprite with uv).
    void spriteRegion(std::string_view id, int texW, int texH, int u, int v, float x, float y, float w, float h,
                      uint32_t argb = 0xFFFFFFFF);
    // A region of a standalone texture ("minecraft:gui/container/inventory") of size texW x texH.
    void texture(std::string_view id, float x, float y, float u, float v, float w, float h, int texW = 256,
                 int texH = 256, uint32_t argb = 0xFFFFFFFF);

    float text(std::string_view text, float x, float y, uint32_t argb = 0xFFFFFFFF, bool shadow = true) {
        return font_.draw(batch_, text, x, y, argb, shadow);
    }
    void centeredText(std::string_view text, float centerX, float y, uint32_t argb = 0xFFFFFFFF, bool shadow = true) {
        font_.drawCentered(batch_, text, centerX, y, argb, shadow);
    }
    int textWidth(std::string_view text) const { return font_.width(text); }

    // A PNG from disk (world icons), cached until forgetFileTextures(); null if unreadable.
    const Texture* fileTexture(const std::filesystem::path& path);
    void forgetFileTextures() { fileTextures_.clear(); }
    void blitTexture(const Texture& texture, float x, float y, float w, float h) {
        batch_.blit(texture, x, y, x + w, y + h, 0, 0, 1, 1);
    }

    // Item icons (16x16 with the top-left at x, y), over everything drawn so far.
    void item(const ItemStack& stack, float x, float y);
    // The stack's count (when not 1, or `countText` instead) in the slot's bottom-right
    // corner (vanilla renderItemDecorations).
    void itemDecorations(const ItemStack& stack, float x, float y, const std::string* countText = nullptr);
    // Vanilla tooltip: lines (§ formatting allowed) in the nine-sliced tooltip frame
    // next to the mouse, kept on screen.
    void tooltip(const std::vector<std::string>& lines, float mouseX, float mouseY, uint32_t restColor = 0xFFA8A8A8);
    // A widget's tooltip (vanilla Tooltip.create): the text wrapped at 170 pixels, all white.
    void textTooltip(std::string_view text, float mouseX, float mouseY) {
        tooltip(font_.split(text, 170.0f), mouseX, mouseY, 0xFFFFFFFF);
    }
    // Clips drawing to a GUI rectangle (vanilla enableScissor; not nested).
    void enableScissor(float x0, float y0, float x1, float y1);
    void disableScissor();
    // The player in a GUI box, turned toward the mouse (vanilla renderEntityInInventoryFollowsMouse).
    void playerFollowingMouse(const Player& player, float x0, float y0, float x1, float y1, float scale, float yOffset,
                              glm::vec2 mouse);
    // What item properties read this frame (time of day, the holder for compasses).
    void setItemContext(const ItemModelContext& ctx) { itemContext_ = ctx; }
    ItemRenderer& items() { return items_; }

private:
    const SpriteInfo& lookup(std::string_view id) const;
    // `uv` in nominal sprite pixels of a nominal size texW x texH.
    void blitPart(const SpriteInfo& s, float texW, float texH, float u, float v, float x, float y, float w, float h,
                  uint32_t argb);
    void blitTiled(const SpriteInfo& s, float x, float y, float w, float h, float u, float v, float tileW,
                   float tileH, float texW, float texH, uint32_t argb);
    void blitNineSliceInner(const SpriteInfo& s, bool stretch, float x, float y, float w, float h, float u, float v,
                            float partW, float partH, float texW, float texH, uint32_t argb);

    const ClientAssets* assets_ = nullptr;
    GuiBatch batch_;
    Font font_;
    ItemRenderer items_;
    MenuBlur blur_;
    ItemModelContext itemContext_;
    glm::ivec2 framebuffer_{0};
    std::unordered_map<std::string, std::unique_ptr<Texture>> fileTextures_;
    std::unique_ptr<ModelDefinition> playerRest_, playerPosed_;
};

} // namespace mc
