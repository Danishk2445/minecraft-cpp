#include "render/ItemRenderer.h"

#include <algorithm>
#include <cstddef>
#include <format>

#include <glm/gtc/matrix_transform.hpp>

#include "core/Fs.h"
#include "core/Log.h"
#include "game/Inventory.h"
#include "gfx/GL.h"
#include "gfx/Texture.h"
#include "render/ClientAssets.h"

namespace mc {
namespace {

constexpr int kDepthSlices = 4096;
constexpr float kPi = 3.14159265358979f;

// JOML rotateYXZ: Ry * Rx * Rz.
glm::mat4 rotateYXZ(const glm::mat4& m, float y, float x, float z) {
    glm::mat4 r = glm::rotate(m, y, glm::vec3(0, 1, 0));
    r = glm::rotate(r, x, glm::vec3(1, 0, 0));
    return glm::rotate(r, z, glm::vec3(0, 0, 1));
}

uint32_t packColor(const glm::vec3& rgb) {
    auto c = [](float v) { return uint32_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return c(rgb.r) | (c(rgb.g) << 8) | (c(rgb.b) << 16) | 0xFF000000u;
}

} // namespace

ItemRenderer::~ItemRenderer() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
}

bool ItemRenderer::init(const ClientAssets& assets) {
    assets_ = &assets;
    if (!shader_.load("gui_item.vert", "gui_item.frag")) return false;
    glCreateVertexArrays(1, &vao_);
    glCreateBuffers(1, &vbo_);
    glEnableVertexArrayAttrib(vao_, 0);
    glVertexArrayAttribFormat(vao_, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, pos));
    glVertexArrayAttribBinding(vao_, 0, 0);
    glEnableVertexArrayAttrib(vao_, 1);
    glVertexArrayAttribFormat(vao_, 1, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, uv));
    glVertexArrayAttribBinding(vao_, 1, 0);
    glEnableVertexArrayAttrib(vao_, 2);
    glVertexArrayAttribFormat(vao_, 2, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(Vertex, color));
    glVertexArrayAttribBinding(vao_, 2, 0);
    // Vanilla Lighting: the level's two lights turned for 3D items (blocks) and flat items.
    const glm::vec3 l0 = glm::normalize(glm::vec3(0.2f, 1.0f, -0.7f)), l1 = glm::normalize(glm::vec3(-0.2f, 1.0f, 0.7f));
    glm::mat4 m3 = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, -1.0f, 1.0f));
    m3 = rotateYXZ(rotateYXZ(m3, 1.0821041f, 3.2375858f, 0.0f), -kPi / 8.0f, kPi * 3.0f / 4.0f, 0.0f);
    const glm::mat4 flat = rotateYXZ(glm::mat4(1.0f), -kPi / 8.0f, kPi * 3.0f / 4.0f, 0.0f);
    lights3d_[0] = glm::normalize(glm::mat3(m3) * l0);
    lights3d_[1] = glm::normalize(glm::mat3(m3) * l1);
    lightsFlat_[0] = glm::normalize(glm::mat3(flat) * l0);
    lightsFlat_[1] = glm::normalize(glm::mat3(flat) * l1);
    return true;
}

void ItemRenderer::beginGui() {
    slice_ = 0;
    glClear(GL_DEPTH_BUFFER_BIT);
}

void ItemRenderer::quads(const ItemStack& stack, const ItemModelContext& ctx, std::vector<ItemQuad>& out) {
    if (stack.empty()) return;
    layers_.clear();
    assets_->itemDefinitions().evaluate(stack.item->id, ctx, &assets_->grassColormap(), layers_);
    buildItemQuads(layers_, ctx.display, assets_->blockEntityModels(), assets_->blockAtlas(), out);
}

void ItemRenderer::drawGui(const ItemStack& stack, float x, float y, glm::vec2 guiSize, const ItemModelContext& context) {
    if (stack.empty()) return;
    ItemModelContext ctx = context;
    ctx.display = DisplayContext::Gui;
    ctx.count = stack.count;
    ctx.maxStack = stack.item->maxStack;
    layers_.clear();
    assets_->itemDefinitions().evaluate(stack.item->id, ctx, &assets_->grassColormap(), layers_);
    quads_.clear();
    buildItemQuads(layers_, DisplayContext::Gui, assets_->blockEntityModels(), assets_->blockAtlas(), quads_);
    if (quads_.empty()) return;
    const glm::vec3* lights = itemGuiLightFront(layers_) ? lightsFlat_ : lights3d_;

    // GuiGraphics.renderItem: centered in the 16x16 slot, 16 pixels per block, y up.
    vertices_.clear();
    const glm::vec3 center(x + 8.0f, y + 8.0f, 0.0f);
    for (const ItemQuad& q : quads_) {
        const glm::vec3 n(q.normal.x, -q.normal.y, q.normal.z);
        const float d = std::max(0.0f, glm::dot(n, lights[0])) + std::max(0.0f, glm::dot(n, lights[1]));
        const float shade = std::min(1.0f, 0.4f + 0.6f * d);
        const glm::vec3 tint = glm::vec3(float((q.tint >> 16) & 255), float((q.tint >> 8) & 255), float(q.tint & 255)) / 255.0f;
        const uint32_t color = packColor(tint * shade);
        Vertex v[4];
        for (size_t k = 0; k < 4; ++k)
            v[k] = {center + glm::vec3(q.pos[k].x, -q.pos[k].y, q.pos[k].z) * 16.0f, q.uv[k], color};
        for (int i : {0, 1, 2, 0, 2, 3}) vertices_.push_back(v[i]);
    }
    drawVertices(assets_->blockAtlasTexture(), guiSize);
}

void ItemRenderer::drawGuiModel(const Texture& texture, const std::vector<ModelQuad>& quads, glm::vec2 guiSize) {
    // Vanilla Lighting.setupForEntityInInventory: two lights from above and in front (GUI y runs down).
    static const glm::vec3 l0 = glm::normalize(glm::vec3(0.2f, -1.0f, 1.0f));
    static const glm::vec3 l1 = glm::normalize(glm::vec3(-0.2f, -1.0f, 0.0f));
    vertices_.clear();
    for (const ModelQuad& q : quads) {
        const float d = std::max(0.0f, glm::dot(q.normal, l0)) + std::max(0.0f, glm::dot(q.normal, l1));
        const uint32_t color = packColor(glm::vec3(std::min(1.0f, 0.4f + 0.6f * d)));
        Vertex v[4];
        for (size_t k = 0; k < 4; ++k) v[k] = {q.pos[k], q.uv[k], color};
        for (int i : {0, 1, 2, 0, 2, 3}) vertices_.push_back(v[i]);
    }
    drawVertices(texture, guiSize);
}

void ItemRenderer::drawVertices(const Texture& texture, glm::vec2 guiSize) {
    if (vertices_.empty()) return;
    if (vertices_.size() > capacity_) {
        capacity_ = std::max(vertices_.size(), capacity_ * 2);
        glNamedBufferData(vbo_, GLsizeiptr(capacity_ * sizeof(Vertex)), nullptr, GL_STREAM_DRAW);
        glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(Vertex));
    }
    glNamedBufferSubData(vbo_, 0, GLsizeiptr(vertices_.size() * sizeof(Vertex)), vertices_.data());

    // Each draw gets the next slice of the depth range, nearer than the last one.
    const int slice = std::min(slice_++, kDepthSlices - 1);
    glDepthRange(1.0 - double(slice + 1) / kDepthSlices, 1.0 - double(slice) / kDepthSlices);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    shader_.use();
    shader_.set("uProj", glm::ortho(0.0f, guiSize.x, guiSize.y, 0.0f, -1000.0f, 1000.0f));
    texture.bind(0);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(vertices_.size()));
    glDepthRange(0.0, 1.0);
    glDisable(GL_DEPTH_TEST);
}

int dumpItemIcons(const ClientAssets& assets, ItemRenderer& renderer, const std::filesystem::path& dir) {
    constexpr int kTile = 32, kPerRow = 64, kSheet = kTile * kPerRow;
    const std::vector<std::string> ids = assets.itemDefinitions().ids();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    {
        std::string index;
        for (const std::string& id : ids) index += id + "\n";
        fs::writeTextAtomic(dir / "items.txt", index);
    }
    Texture color, depth;
    color.create(kSheet, kSheet, 1);
    depth.createDepth(kSheet, kSheet);
    unsigned fbo = 0;
    glCreateFramebuffers(1, &fbo);
    glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, color.id(), 0);
    glNamedFramebufferTexture(fbo, GL_DEPTH_STENCIL_ATTACHMENT, depth.id(), 0);
    if (glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        log::error("item icons: framebuffer incomplete");
        glDeleteFramebuffers(1, &fbo);
        return 1;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, kSheet, kSheet);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    renderer.beginGui();
    ItemModelContext ctx; // noon, no holder
    std::vector<Item> items(ids.size());
    for (size_t i = 0; i < ids.size(); ++i) {
        items[i].id = ids[i];
        const ItemStack stack{&items[i], 1};
        renderer.drawGui(stack, float(i % kPerRow) * 16.0f, float(i / kPerRow) * 16.0f,
                         glm::vec2(kSheet / 2, kSheet / 2), ctx);
    }
    Image sheet(kSheet, kSheet);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, kSheet, kSheet, GL_RGBA, GL_UNSIGNED_BYTE, sheet.pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    // GL rows run bottom-up.
    for (int y = 0; y < kSheet / 2; ++y)
        std::swap_ranges(sheet.px(0, y), sheet.px(0, y) + size_t(kSheet) * 4, sheet.px(0, kSheet - 1 - y));

    int failures = 0;
    std::string report;
    for (size_t i = 0; i < ids.size(); ++i) {
        const int x0 = int(i % kPerRow) * kTile, y0 = int(i / kPerRow) * kTile;
        int drawn = 0, magenta = 0;
        for (int y = y0; y < y0 + kTile; ++y)
            for (int x = x0; x < x0 + kTile; ++x) {
                const uint8_t* p = sheet.px(x, y);
                if (p[3] == 0) continue;
                ++drawn;
                // The missing texture's magenta (#F800F8) at any shade: no green at all, red and
                // blue alike (bubble coral's magenta keeps some green).
                if (p[0] > 60 && p[1] == 0 && std::abs(int(p[0]) - int(p[2])) <= 3) ++magenta;
            }
        const bool emptyOk = ids[i] == "minecraft:air";
        if ((drawn == 0 && !emptyOk) || magenta > 0) {
            ++failures;
            report += std::format("{} {}\n", ids[i], drawn == 0 ? "empty" : "magenta");
        }
    }
    sheet.savePng(dir / "items.png");
    fs::writeTextAtomic(dir / "failures.txt", report);
    log::info("Item icons: {} items, {} failures (sheet {})", ids.size(), failures, (dir / "items.png").string());
    if (!report.empty()) log::warn("item icon failures:\n{}", report);
    return failures;
}

} // namespace mc
