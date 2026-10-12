// EntityRenderer.h -- draws entity models every frame: posed ModelParts with
// their own textures, vanilla entity lighting (two directional lights, the
// lightmap at the entity) and fog, plus the round shadows under entities.
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "entity/model/ItemGeometry.h"
#include "entity/model/ModelPart.h"
#include "gfx/Shader.h"
#include "gfx/Texture.h"
#include "render/ChunkRenderer.h"
#include "ui/Font.h"

namespace mc {

class ClientAssets;
class Level;
class Player;

class ExperienceOrb;
class ItemEntity;

class EntityRenderer {
public:
    ~EntityRenderer();
    bool init(const ClientAssets& assets);

    // Starts a frame's submissions; positions become relative to `cameraPos`. Billboards
    // (experience orbs) face the camera along its right and up vectors.
    void begin(const glm::dvec3& cameraPos, const glm::vec3& cameraRight = {1, 0, 0},
               const glm::vec3& cameraUp = {0, 1, 0});
    bool shadows = true; // the Entity Shadows option
    // A posed model: `transform` maps model root space (blocks) to world space
    // relative to `origin`. `light` is (block, sky), 0..15.
    // Below full `alpha` the model is blended (vanilla's translucent entity render types).
    // `hurt` tints it red (vanilla OverlayTexture while hurt or dying).
    void submitModel(const ModelPart& root, int texWidth, int texHeight, std::string_view texture,
                     const glm::dvec3& origin, const glm::mat4& transform, glm::vec2 light,
                     uint32_t tint = 0xFFFFFF, float alpha = 1.0f, bool hurt = false);
    // Vanilla EntityRenderDispatcher.renderShadow: a dark disc of `radius` on the full
    // blocks under `feet`, fading with height above them, darkness and camera distance.
    void submitShadow(const Level& level, const glm::dvec3& feet, float radius, int skyDarken, float strength = 1.0f);
    // Vanilla ItemEntityRenderer: the stack bobbing and spinning on the ground, drawn 1-5
    // times by its count, with a small shadow. `at` overrides its position (pickups).
    void submitGroundItem(const ItemEntity& item, const Level& level, float partialTick, int skyDarken,
                          const glm::dvec3* at = nullptr);
    // Vanilla ExperienceOrbRenderer: a pulsing green-yellow sprite facing the camera.
    void submitExperienceOrb(const ExperienceOrb& orb, const Level& level, float partialTick, int skyDarken,
                             const glm::dvec3* at = nullptr);
    // Item quads (block atlas) placed by `transform` relative to `origin`.
    void submitItem(const std::vector<ItemQuad>& quads, const glm::dvec3& origin, const glm::mat4& transform,
                    glm::vec2 light);
    // Text in the world (sign lines): glyph quads in text pixels placed by `transform`
    // relative to `origin`, drawn with the font's texture.
    void submitText(const std::vector<Font::GlyphQuad>& quads, const Texture& fontTexture, const glm::dvec3& origin,
                    const glm::mat4& transform, glm::vec2 light);
    // The player model (Steve), posed at `partialTick`, with its held items and shadow.
    void submitPlayer(const Player& player, const Level& level, float partialTick, int skyDarken);

    // Draws everything submitted. The lightmap must be bound to texture unit 1.
    void render(const glm::mat4& viewProj, const ChunkRenderer::Fog& fog);

private:
    struct Vertex {
        glm::vec3 pos; // camera-relative
        glm::vec2 uv;
        uint32_t color; // RGBA8: tint * entity shading
        glm::vec2 light;
        float overlay = 0.0f; // how far toward red (vanilla's hurt overlay)
    };
    enum class Pass : uint8_t { Cutout, Shadow, Translucent };
    struct Batch {
        const Texture* texture;
        size_t first, count; // vertices
        Pass pass;
    };
    const Texture& texture(std::string_view id);
    // Vertices from `first` to the end, drawn with `tex` in `pass`.
    void addBatch(const Texture* tex, size_t first, Pass pass);

    const ClientAssets* assets_ = nullptr;
    Shader shader_;
    unsigned vao_ = 0, vbo_ = 0;
    glm::dvec3 cameraPos_{0.0};
    glm::vec3 cameraRight_{1, 0, 0}, cameraUp_{0, 1, 0};
    std::vector<Vertex> vertices_;
    std::vector<Batch> batches_;
    std::vector<ModelQuad> quads_;
    std::vector<ItemQuad> itemQuads_;
    std::vector<ItemLayer> layers_;
    // Player models at rest and a working copy posed each frame.
    ModelDefinition playerRest_, playerPosed_;
};

} // namespace mc
