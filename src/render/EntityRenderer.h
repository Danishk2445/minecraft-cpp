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

#include "entity/model/ModelPart.h"
#include "gfx/Shader.h"
#include "gfx/Texture.h"
#include "render/ChunkRenderer.h"

namespace mc {

class Level;
class Player;
class ResourcePack;

class EntityRenderer {
public:
    ~EntityRenderer();
    bool init(const ResourcePack& pack);

    // Starts a frame's submissions; positions become relative to `cameraPos`.
    void begin(const glm::dvec3& cameraPos);
    // A posed model: `transform` maps model root space (blocks) to world space
    // relative to `origin`. `light` is (block, sky), 0..15.
    // Below full `alpha` the model is blended (vanilla's translucent entity render types).
    void submitModel(const ModelPart& root, int texWidth, int texHeight, std::string_view texture,
                     const glm::dvec3& origin, const glm::mat4& transform, glm::vec2 light,
                     uint32_t tint = 0xFFFFFF, float alpha = 1.0f);
    // Vanilla EntityRenderDispatcher.renderShadow: a dark disc of `radius` on the full
    // blocks under `feet`, fading with height above them, darkness and camera distance.
    void submitShadow(const Level& level, const glm::dvec3& feet, float radius, int skyDarken);
    // The player model (Steve), posed at `partialTick`, with its shadow.
    void submitPlayer(const Player& player, const Level& level, float partialTick, int skyDarken);

    // Draws everything submitted. The lightmap must be bound to texture unit 1.
    void render(const glm::mat4& viewProj, const ChunkRenderer::Fog& fog);

private:
    struct Vertex {
        glm::vec3 pos; // camera-relative
        glm::vec2 uv;
        uint32_t color; // RGBA8: tint * entity shading
        glm::vec2 light;
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

    const ResourcePack* pack_ = nullptr;
    Shader shader_;
    unsigned vao_ = 0, vbo_ = 0;
    std::unordered_map<std::string, std::unique_ptr<Texture>> textures_;
    Texture missing_;
    glm::dvec3 cameraPos_{0.0};
    std::vector<Vertex> vertices_;
    std::vector<Batch> batches_;
    std::vector<ModelQuad> quads_;
    // Player models at rest and a working copy posed each frame.
    ModelDefinition playerRest_, playerPosed_;
};

} // namespace mc
