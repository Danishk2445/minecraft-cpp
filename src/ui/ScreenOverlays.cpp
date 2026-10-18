#include "ui/ScreenOverlays.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "gfx/GL.h"
#include "render/ClientAssets.h"
#include "resources/Atlas.h"
#include "ui/GuiGraphics.h"

namespace mc {
namespace {

constexpr float kHandHalfFovTan = 0.70020753f; // tan(35 degrees): the hands' projection

// ARGB from 0..1 channels (vanilla ARGB.colorFromFloat: floor(f * 255)).
uint32_t argb(float a, float r, float g, float b) {
    const auto c = [](float f) { return uint32_t(std::floor(std::clamp(f, 0.0f, 1.0f) * 255.0f)); };
    return c(a) << 24 | c(r) << 16 | c(g) << 8 | c(b);
}

} // namespace

void renderScreenOverlays(GuiGraphics& gui, const ScreenOverlays& o, const Texture& blockAtlas) {
    GuiBatch& batch = gui.batch();
    const glm::vec2 size = gui.size();
    const float aspect = size.x / std::max(size.y, 1.0f);
    // A view-space point (camera at the origin looking down -z) to GUI pixels.
    const auto toScreen = [&](const glm::vec3& p) {
        const float depth = -p.z * kHandHalfFovTan;
        return glm::vec2((p.x / (depth * aspect) * 0.5f + 0.5f) * size.x, (0.5f - p.y / depth * 0.5f) * size.y);
    };
    // Vanilla's quads at z = -0.5 spanning -1..1: far bigger than the screen, so only their
    // middles show.
    const glm::vec2 topLeft = toScreen({-1.0f, 1.0f, -0.5f}), bottomRight = toScreen({1.0f, -1.0f, -0.5f});

    // renderTex: the block's particle texture at a tenth of its brightness, mirrored sideways.
    if (o.inWall) {
        const SpriteInfo& s = *o.inWall;
        batch.blit(blockAtlas, topLeft.x, topLeft.y, bottomRight.x, bottomRight.y, s.u1, s.v0, s.u0, s.v1,
                   argb(1.0f, 0.1f, 0.1f, 0.1f));
    }
    // renderWater: the underwater texture four times across, lit like the eyes' block,
    // sliding with the view's yaw and pitch.
    if (o.underwater) {
        const Texture& tex = gui.assets().texture("minecraft:misc/underwater");
        const_cast<Texture&>(tex).setWrap(GL_REPEAT); // the asset cache's copy, only drawn here
        const float m = -o.yaw / 64.0f, n = o.pitch / 64.0f, f = o.underwaterBrightness;
        batch.blit(tex, topLeft.x, topLeft.y, bottomRight.x, bottomRight.y, 4.0f + m, n, m, 4.0f + n,
                   argb(0.1f, f, f, f));
    }
    // renderFire: two flames of fire_1 low on either side, turned 10 degrees inward.
    if (o.fire) {
        const SpriteInfo& s = *o.fire;
        for (int r = 0; r < 2; ++r) {
            const float side = float(r * 2 - 1);
            glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(-side * 0.24f, -0.3f, 0.0f));
            m = glm::rotate(m, glm::radians(side * 10.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            const auto corner = [&](float x, float y) { return toScreen(glm::vec3(m * glm::vec4(x, y, -0.5f, 1.0f))); };
            const glm::vec2 quad[4] = {corner(-0.5f, 0.5f), corner(-0.5f, -0.5f), corner(0.5f, -0.5f), corner(0.5f, 0.5f)};
            batch.blitQuad(blockAtlas, quad, s.u1, s.v0, s.u0, s.v1, argb(0.9f, 1.0f, 1.0f, 1.0f));
        }
    }
    // Gui.renderVignette: darkens toward the edges by the vignette brightness.
    if (o.vignette >= 0.0f) {
        const float h = std::clamp(o.vignette, 0.0f, 1.0f);
        batch.setBlend(GuiBatch::Blend::Darken);
        batch.blit(gui.assets().texture("minecraft:misc/vignette"), 0.0f, 0.0f, size.x, size.y, 0.0f, 0.0f, 1.0f, 1.0f,
                   argb(1.0f, h, h, h));
        batch.setBlend(GuiBatch::Blend::Alpha);
    }
    // renderTextureOverlay: the carved pumpkin's blur over everything.
    if (o.pumpkin)
        batch.blit(gui.assets().texture("minecraft:misc/pumpkinblur"), 0.0f, 0.0f, size.x, size.y, 0.0f, 0.0f, 1.0f,
                   1.0f);
}

} // namespace mc
