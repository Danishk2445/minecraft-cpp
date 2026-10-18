#include "ui/GuiGraphics.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "entity/Player.h"
#include "entity/model/EntityModels.h"
#include "entity/model/HumanoidModel.h"
#include "game/Inventory.h"
#include "gfx/GL.h"
#include "render/ClientAssets.h"
#include "resources/ResourcePack.h"

namespace mc {

bool GuiGraphics::init(const ClientAssets& assets) {
    assets_ = &assets;
    return batch_.init() && font_.load(assets.pack()) && items_.init(assets) && blur_.init();
}

void GuiGraphics::blurBehind(int radius) {
    if (radius < 1) return;
    batch_.flush();
    blur_.apply(framebuffer_, radius);
    batch_.restoreState();
}

void GuiGraphics::begin(glm::ivec2 framebuffer, int scale) {
    framebuffer_ = framebuffer;
    scissors_.clear();
    batch_.begin(framebuffer, scale);
    items_.beginGui();
}

const Texture* GuiGraphics::fileTexture(const std::filesystem::path& path) {
    const std::string key = path.string();
    auto it = fileTextures_.find(key);
    if (it == fileTextures_.end()) {
        std::unique_ptr<Texture> tex;
        if (auto img = Image::loadPng(path)) {
            tex = std::make_unique<Texture>();
            tex->create(img->width, img->height, 1);
            tex->upload(0, 0, 0, *img);
            tex->setFilter(GL_LINEAR, GL_NEAREST);
        }
        it = fileTextures_.emplace(key, std::move(tex)).first;
    }
    return it->second.get();
}

void GuiGraphics::item(const ItemStack& stack, float x, float y) {
    if (stack.empty()) return;
    batch_.flush(); // keep the drawing order
    items_.drawGui(stack, x, y, size(), itemContext_);
}

void GuiGraphics::itemDecorations(const ItemStack& stack, float x, float y, const std::string* countText) {
    if (stack.empty() && !countText) return;
    if (!countText && stack.count == 1) return;
    const std::string count = countText ? *countText : std::to_string(stack.count);
    text(count, x + 19 - 2 - float(textWidth(count)), y + 6 + 3, 0xFFFFFFFF);
}

void GuiGraphics::tooltip(const std::vector<std::string>& lines, float mouseX, float mouseY, uint32_t restColor) {
    if (lines.empty()) return;
    // ClientTextTooltip sizes: 10 per line, plus 2 under the title when there are more.
    float width = 0.0f;
    for (const std::string& l : lines) width = std::max(width, float(textWidth(l)));
    const float height = lines.size() == 1 ? 8.0f : 8.0f + 2.0f + 10.0f * float(lines.size() - 1);
    // DefaultTooltipPositioner: up and right of the mouse, flipped or clamped to stay visible.
    float x = mouseX + 12, y = mouseY - 12;
    if (x + width > size().x) x = std::max(x - 24 - width, 4.0f);
    if (y + height + 3 > size().y) y = size().y - height - 3;
    // TooltipRenderUtil.renderTooltipBackground
    sprite("tooltip/background", x - 3 - 9, y - 3 - 9, width + 6 + 18, height + 6 + 18);
    sprite("tooltip/frame", x - 3 - 9, y - 3 - 9, width + 6 + 18, height + 6 + 18);
    float ly = y;
    for (size_t i = 0; i < lines.size(); ++i) {
        text(lines[i], x, ly, i == 0 ? 0xFFFFFFFF : restColor);
        ly += i == 0 ? 12.0f : 10.0f;
    }
}

void GuiGraphics::enableScissor(float x0, float y0, float x1, float y1) {
    batch_.flush();
    glm::vec4 r(x0, y0, x1, y1);
    if (!scissors_.empty()) {
        const glm::vec4& o = scissors_.back();
        r = glm::vec4(std::max(r.x, o.x), std::max(r.y, o.y), std::min(r.z, o.z), std::min(r.w, o.w));
        r.z = std::max(r.z, r.x);
        r.w = std::max(r.w, r.y);
    }
    scissors_.push_back(r);
    applyScissor();
}

void GuiGraphics::disableScissor() {
    batch_.flush();
    if (!scissors_.empty()) scissors_.pop_back();
    applyScissor();
}

void GuiGraphics::applyScissor() const {
    if (scissors_.empty()) {
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    const glm::vec4& r = scissors_.back();
    const float s = float(scale());
    glEnable(GL_SCISSOR_TEST);
    glScissor(GLint(r.x * s), GLint(float(framebuffer_.y) - r.w * s), GLsizei((r.z - r.x) * s), GLsizei((r.w - r.y) * s));
}

void GuiGraphics::playerFollowingMouse(const Player& player, float x0, float y0, float x1, float y1, float scale,
                                       float yOffset, glm::vec2 mouse) {
    if (!playerRest_) {
        playerRest_ = std::make_unique<ModelDefinition>(playerModel(false));
        playerPosed_ = std::make_unique<ModelDefinition>(*playerRest_);
    }
    const float cx = (x0 + x1) / 2.0f, cy = (y0 + y1) / 2.0f;
    const float yawFactor = std::atan((cx - mouse.x) / 40.0f), pitchFactor = std::atan((cy - mouse.y) / 40.0f);
    // The entity's rotations while drawn: body, look and head follow the mouse.
    HumanoidPose pose;
    pose.ageInTicks = float(player.tickCount);
    pose.bodyYaw = 180.0f + yawFactor * 20.0f;
    pose.headYaw = yawFactor * 20.0f; // (180 + 40h) - (180 + 20h)
    pose.headPitch = -pitchFactor * 20.0f;
    pose.crouching = player.crouching;
    pose.rightArmItem = !player.inventory.selectedStack().empty();
    pose.leftArmItem = !player.inventory.offhand.empty();
    resetPose(playerPosed_->root, playerRest_->root);
    animateHumanoid(playerPosed_->root, pose);
    // renderEntityInInventory: T(center, 50) S(s, s, -s) T(0, height/2 + yOffset) Rz(pi) Rx(pitch * 20).
    glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(cx, cy, 50.0f));
    m = glm::scale(m, glm::vec3(scale, scale, -scale));
    m = glm::translate(m, glm::vec3(0.0f, player.height / 2.0f + yOffset, 0.0f));
    m = glm::rotate(m, 3.14159265f, glm::vec3(0, 0, 1));
    m = glm::rotate(m, glm::radians(pitchFactor * 20.0f), glm::vec3(1, 0, 0));
    m = m * livingModelTransform(pose.bodyYaw, 0.9375f, player.crouching ? -0.125f : 0.0f);
    std::vector<ModelQuad> quads;
    bakeModel(playerPosed_->root, m, playerRest_->texWidth, playerRest_->texHeight, quads);
    enableScissor(x0, y0, x1, y1);
    items_.drawGuiModel(assets_->texture("minecraft:entity/player/wide/steve"), quads, size());
    disableScissor();
}

void GuiGraphics::end() { batch_.end(); }

void GuiGraphics::fillGradient(float x0, float y0, float x1, float y1, uint32_t top, uint32_t bottom) {
    // One quad per row of GUI pixels keeps it in the batch's flat-colored quads.
    const int rows = std::max(1, int(y1 - y0));
    for (int i = 0; i < rows; ++i) {
        const float t = (float(i) + 0.5f) / float(rows);
        uint32_t c = 0;
        for (int sh = 0; sh < 32; sh += 8) {
            const float a = float((top >> sh) & 0xFF), b = float((bottom >> sh) & 0xFF);
            c |= uint32_t(a + (b - a) * t + 0.5f) << sh;
        }
        const float ya = y0 + (y1 - y0) * float(i) / float(rows), yb = y0 + (y1 - y0) * float(i + 1) / float(rows);
        batch_.fill(x0, ya, x1, yb, c);
    }
}

const SpriteInfo& GuiGraphics::lookup(std::string_view id) const {
    const Atlas& atlas = assets_->guiAtlas();
    return atlas.sprite(id.find(':') == std::string_view::npos ? "minecraft:" + std::string(id) : std::string(id));
}

void GuiGraphics::blitPart(const SpriteInfo& s, float texW, float texH, float u, float v, float x, float y, float w,
                           float h, uint32_t argb) {
    if (w <= 0.0f || h <= 0.0f) return;
    const float su0 = s.u0 + (s.u1 - s.u0) * (u / texW), su1 = s.u0 + (s.u1 - s.u0) * ((u + w) / texW);
    const float sv0 = s.v0 + (s.v1 - s.v0) * (v / texH), sv1 = s.v0 + (s.v1 - s.v0) * ((v + h) / texH);
    batch_.blit(assets_->guiAtlasTexture(), x, y, x + w, y + h, su0, sv0, su1, sv1, argb);
}

void GuiGraphics::blitTiled(const SpriteInfo& s, float x, float y, float w, float h, float u, float v, float tileW,
                            float tileH, float texW, float texH, uint32_t argb) {
    if (w <= 0.0f || h <= 0.0f || tileW <= 0.0f || tileH <= 0.0f) return;
    for (float i = 0; i < w; i += tileW)
        for (float k = 0; k < h; k += tileH)
            blitPart(s, texW, texH, u, v, x + i, y + k, std::min(tileW, w - i), std::min(tileH, h - k), argb);
}

void GuiGraphics::blitNineSliceInner(const SpriteInfo& s, bool stretch, float x, float y, float w, float h, float u,
                                     float v, float partW, float partH, float texW, float texH, uint32_t argb) {
    if (w <= 0.0f || h <= 0.0f) return;
    if (!stretch) {
        blitTiled(s, x, y, w, h, u, v, partW, partH, texW, texH, argb);
        return;
    }
    const float su0 = s.u0 + (s.u1 - s.u0) * (u / texW), su1 = s.u0 + (s.u1 - s.u0) * ((u + partW) / texW);
    const float sv0 = s.v0 + (s.v1 - s.v0) * (v / texH), sv1 = s.v0 + (s.v1 - s.v0) * ((v + partH) / texH);
    batch_.blit(assets_->guiAtlasTexture(), x, y, x + w, y + h, su0, sv0, su1, sv1, argb);
}

void GuiGraphics::sprite(std::string_view id, float x, float y, float w, float h, uint32_t argb) {
    const SpriteInfo& s = lookup(id);
    const GuiScaling scaling = s.gui.value_or(GuiScaling{});
    if (scaling.type == GuiScaling::Type::Stretch) {
        batch_.blit(assets_->guiAtlasTexture(), x, y, x + w, y + h, s.u0, s.v0, s.u1, s.v1, argb);
        return;
    }
    const float nw = float(scaling.width), nh = float(scaling.height);
    if (scaling.type == GuiScaling::Type::Tile) {
        blitTiled(s, x, y, w, h, 0, 0, nw, nh, nw, nh, argb);
        return;
    }
    // Vanilla blitNineSlicedSprite: corners keep their size, edges and center tile (or stretch).
    const float l = std::min(float(scaling.left), w / 2), r = std::min(float(scaling.right), w / 2);
    const float t = std::min(float(scaling.top), h / 2), b = std::min(float(scaling.bottom), h / 2);
    const bool stretch = scaling.stretchInner;
    if (w == nw && h == nh) {
        blitPart(s, nw, nh, 0, 0, x, y, w, h, argb);
    } else if (h == nh) {
        blitPart(s, nw, nh, 0, 0, x, y, l, h, argb);
        blitNineSliceInner(s, stretch, x + l, y, w - r - l, h, l, 0, nw - r - l, nh, nw, nh, argb);
        blitPart(s, nw, nh, nw - r, 0, x + w - r, y, r, h, argb);
    } else if (w == nw) {
        blitPart(s, nw, nh, 0, 0, x, y, w, t, argb);
        blitNineSliceInner(s, stretch, x, y + t, w, h - b - t, 0, t, nw, nh - b - t, nw, nh, argb);
        blitPart(s, nw, nh, 0, nh - b, x, y + h - b, w, b, argb);
    } else {
        blitPart(s, nw, nh, 0, 0, x, y, l, t, argb);
        blitNineSliceInner(s, stretch, x + l, y, w - r - l, t, l, 0, nw - r - l, t, nw, nh, argb);
        blitPart(s, nw, nh, nw - r, 0, x + w - r, y, r, t, argb);
        blitPart(s, nw, nh, 0, nh - b, x, y + h - b, l, b, argb);
        blitNineSliceInner(s, stretch, x + l, y + h - b, w - r - l, b, l, nh - b, nw - r - l, b, nw, nh, argb);
        blitPart(s, nw, nh, nw - r, nh - b, x + w - r, y + h - b, r, b, argb);
        blitNineSliceInner(s, stretch, x, y + t, l, h - b - t, 0, t, l, nh - b - t, nw, nh, argb);
        blitNineSliceInner(s, stretch, x + l, y + t, w - r - l, h - b - t, l, t, nw - r - l, nh - b - t, nw, nh, argb);
        blitNineSliceInner(s, stretch, x + w - r, y + t, r, h - b - t, nw - r, t, r, nh - b - t, nw, nh, argb);
    }
}

void GuiGraphics::spriteRegion(std::string_view id, int texW, int texH, int u, int v, float x, float y, float w,
                               float h, uint32_t argb) {
    blitPart(lookup(id), float(texW), float(texH), float(u), float(v), x, y, w, h, argb);
}

void GuiGraphics::texture(std::string_view id, float x, float y, float u, float v, float w, float h, int texW,
                          int texH, uint32_t argb) {
    batch_.blit(assets_->texture(id), x, y, x + w, y + h, u / float(texW), v / float(texH), (u + w) / float(texW),
                (v + h) / float(texH), argb);
}

} // namespace mc
