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
#include "world/block/BlockRegistry.h"

namespace mc {

class ClientAssets;
class Level;
class Player;

class ExperienceOrb;
class FallingBlockEntity;
class ItemEntity;
class LightningBolt;
class PrimedTnt;

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
    // `overlay` (RGBA8, R in the low byte) mixes its color over the model by its alpha
    // before the light: vanilla OverlayTexture, red while hurt, white for flashing TNT.
    static constexpr uint32_t kHurtOverlay = 0x4D0000FFu;  // (255, 0, 0) at 77/255
    static constexpr uint32_t kWhiteOverlay = 0xC0FFFFFFu; // (255, 255, 255) at 192/255
    void submitModel(const ModelPart& root, int texWidth, int texHeight, std::string_view texture,
                     const glm::dvec3& origin, const glm::mat4& transform, glm::vec2 light,
                     uint32_t tint = 0xFFFFFF, float alpha = 1.0f, uint32_t overlay = 0);
    // A block state's model as an entity (vanilla FallingBlockRenderer, TntRenderer):
    // `transform` places the block's 0..1 space relative to `origin`; directional block
    // shading, flat `light`, biome-default tints.
    void submitBlock(const BlockRegistry& registry, StateId state, const BlockPos& variantPos,
                     const glm::dvec3& origin, const glm::mat4& transform, glm::vec2 light, uint32_t overlay = 0);
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
    // Vanilla FallingBlockRenderer: the block's model centered on the entity (its variant
    // from where it started falling), lit at its top, with a shadow.
    void submitFallingBlock(const FallingBlockEntity& block, const Level& level, float partialTick, int skyDarken);
    // Vanilla TntRenderer: the TNT block swelling in its last half second and flashing
    // white every 5 ticks.
    void submitPrimedTnt(const PrimedTnt& tnt, const Level& level, float partialTick, int skyDarken);
    // Vanilla LightningBoltRenderer: the bolt's branching glow, 128 blocks tall, drawn
    // additively (its shape from the bolt's seed).
    void submitLightning(const LightningBolt& bolt, float partialTick);
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
        uint32_t overlay = 0; // RGBA8: a color mixed in by its alpha (see submitModel)
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
    // Lightning: position and color, blended additively.
    struct GlowVertex {
        glm::vec3 pos; // camera-relative
        uint32_t color; // RGBA8
    };
    Shader glowShader_;
    unsigned glowVao_ = 0, glowVbo_ = 0;
    std::vector<GlowVertex> glow_;
    glm::dvec3 cameraPos_{0.0};
    glm::vec3 cameraRight_{1, 0, 0}, cameraUp_{0, 1, 0};
    std::vector<Vertex> vertices_;
    std::vector<Batch> batches_;
    std::vector<ModelQuad> quads_;
    std::vector<ItemQuad> itemQuads_;
    std::vector<ItemLayer> layers_;
    std::vector<const BakedModel*> blockModels_;
    // Player models at rest and a working copy posed each frame.
    ModelDefinition playerRest_, playerPosed_;
};

} // namespace mc
