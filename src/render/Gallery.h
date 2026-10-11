// Gallery.h -- M1 debug views of every block state the pack defines: a 3D grid
// with a fly camera, and offscreen "model sheets" (one tile per state) used by
// automated checks. The chunk renderer's debug world replaces the 3D grid in M2.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/Math.h"
#include "gfx/Camera.h"
#include "gfx/Shader.h"
#include "gfx/Texture.h"
#include "render/ClientAssets.h"

namespace mc {

// One concrete block state, with properties taken from the pack's own keys.
struct VisualState {
    const BlockStateDef* def = nullptr;
    PropList props; // sorted by name
    std::vector<const BakedChoice*> choices;

    std::string describe() const; // "minecraft:oak_stairs[facing=north,half=top,shape=straight]"
    std::string_view prop(std::string_view name) const;
    bool hasGeometry() const;
};

// Variant files contribute one state per key; multipart files the product of the
// (completed) values their conditions mention.
std::vector<VisualState> enumerateVisualStates(const BlockModels& models);

struct MeshVertex {
    glm::vec3 pos;
    glm::vec2 uv;
    uint32_t color; // RGBA8, little-endian (R in the low byte)
};

struct SimpleMesh {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    void clear() {
        vertices.clear();
        indices.clear();
    }
};

// Appends a state's quads with flat directional shading and default biome tints.
// `xform` maps block-local positions (0..1) to output space; `seedPos` chooses
// random variants.
void appendStateQuads(const VisualState& state, const BlockPos& seedPos, const glm::mat4& xform,
                      const ClientAssets& assets, SimpleMesh& opaque, SimpleMesh& translucent);

// GPU copy of a SimpleMesh (interleaved VBO + IBO + VAO).
class GpuMesh {
public:
    ~GpuMesh();
    void upload(const SimpleMesh& mesh);
    void draw() const;
    size_t indexCount() const { return indexCount_; }

private:
    GpuBuffer vbo_, ibo_;
    unsigned vao_ = 0;
    size_t indexCount_ = 0;
};

class Gallery {
public:
    bool init(const ClientAssets& assets);
    void render(const Camera& camera, glm::ivec2 viewport);
    glm::dvec3 startPosition() const { return start_; }
    size_t stateCount() const { return stateCount_; }

private:
    const ClientAssets* assets_ = nullptr;
    Shader shader_;
    GpuMesh opaque_, translucent_;
    glm::dvec3 start_{0.0};
    size_t stateCount_ = 0;
};

// Renders every visual state into 32px tiles on 2048px sheets under `dir`
// (sheet_N.png, report.json). Tiles for the states listed in `goldenList` (one
// describe() string per line) are also saved to dir/tiles/. Returns the number
// of failing states: a missing-texture pixel anywhere, or an empty tile for a
// state whose model has geometry.
int dumpModelSheets(const ClientAssets& assets, const std::filesystem::path& dir,
                    const std::filesystem::path& goldenList);

} // namespace mc
