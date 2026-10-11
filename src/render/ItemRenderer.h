// ItemRenderer.h -- item icons in the GUI (vanilla GuiGraphics.renderItem): the
// item's GUI display transform at 16 GUI pixels per block, lit by vanilla's GUI
// lights (3D or flat), each icon in front of everything drawn before it.
#pragma once

#include <filesystem>
#include <vector>

#include <glm/glm.hpp>

#include "entity/model/ItemGeometry.h"
#include "entity/model/ModelPart.h"
#include "gfx/Shader.h"

namespace mc {

class ClientAssets;
class Texture;
struct ItemStack;

class ItemRenderer {
public:
    ~ItemRenderer();
    bool init(const ClientAssets& assets);
    // Starts a GUI pass: icons get fresh depth.
    void beginGui();
    // Draws the icon of `stack` (16x16 GUI pixels with its top-left at x, y) over
    // whatever is already drawn. `guiSize` is the GUI viewport in GUI pixels.
    void drawGui(const ItemStack& stack, float x, float y, glm::vec2 guiSize, const ItemModelContext& ctx);

    // The item's quads in a display context (for in-world rendering and tests).
    void quads(const ItemStack& stack, const ItemModelContext& ctx, std::vector<ItemQuad>& out);

    // An entity model in the GUI (vanilla renderEntityInInventory): `quads` already in
    // GUI pixels (y down, z toward the viewer), lit by the inventory entity lights.
    void drawGuiModel(const Texture& texture, const std::vector<ModelQuad>& quads, glm::vec2 guiSize);

private:
    void drawVertices(const Texture& texture, glm::vec2 guiSize);
    struct Vertex {
        glm::vec3 pos;
        glm::vec2 uv;
        uint32_t color;
    };
    const ClientAssets* assets_ = nullptr;
    Shader shader_;
    unsigned vao_ = 0, vbo_ = 0;
    size_t capacity_ = 0;
    int slice_ = 0;
    glm::vec3 lights3d_[2], lightsFlat_[2];
    std::vector<ItemLayer> layers_;
    std::vector<ItemQuad> quads_;
    std::vector<Vertex> vertices_;
};

// Renders every item definition's GUI icon into a sheet in `dir` (32 pixels per
// icon, ids listed in items.txt) and reports icons that are empty or show the
// missing texture. Returns the number of failures.
int dumpItemIcons(const ClientAssets& assets, ItemRenderer& renderer, const std::filesystem::path& dir);

} // namespace mc
