// ScreenOverlays.h -- what covers the view in first person, drawn before the HUD:
// vanilla ScreenEffectRenderer (the texture of a block the eyes are in, the water texture
// under water, flames while burning) and Gui.renderCameraOverlays (the vignette that
// darkens the edges in the dark, with Fancy graphics; a worn carved pumpkin's blur).
#pragma once

namespace mc {

class GuiGraphics;
class Texture;
struct SpriteInfo;

struct ScreenOverlays {
    const SpriteInfo* inWall = nullptr; // block atlas sprite of the block the eyes are in
    bool underwater = false;
    float underwaterBrightness = 1.0f; // vanilla LightTexture.getBrightness at the eyes
    float yaw = 0.0f, pitch = 0.0f;    // the water texture slides as the view turns
    const SpriteInfo* fire = nullptr;  // block atlas "block/fire_1" while burning
    float vignette = -1.0f;            // vanilla Gui.vignetteBrightness; below 0: none (Fast graphics)
    bool pumpkin = false;
};

// Full-screen quads in the GUI pass (begun), placed and mapped as vanilla's quads half a
// block in front of the eyes in the hands' 70-degree projection.
void renderScreenOverlays(GuiGraphics& gui, const ScreenOverlays& overlays, const Texture& blockAtlas);

// Vanilla LightTexture.getBrightness in the overworld: light level 0..15 to 0..1.
inline float lightBrightness(int level) {
    const float f = float(level) / 15.0f;
    return f / (4.0f - 3.0f * f);
}

} // namespace mc
