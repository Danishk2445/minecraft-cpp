// ModelPart.h -- vanilla entity model geometry (ModelPart / LayerDefinition):
// parts holding cubes with box-UV texture layout, a pose (pivot, rotation,
// scale) and child parts. GL-free: baked into quads for chunk meshes (block
// entities) or drawn each frame (the player, mobs).
#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

namespace mc {

struct ModelCube {
    glm::vec3 from{0.0f}; // model units (1/16 block)
    glm::vec3 size{0.0f};
    glm::vec2 texOffs{0.0f}; // texels
    float grow = 0.0f;       // vanilla CubeDeformation: inflates the box, not its UVs
    bool mirror = false;     // swaps the box's x sides in the texture
    uint8_t faces = 0x3F;    // visible faces, bit per Direction (D, U, N, S, W, E)
};

struct PartPose {
    glm::vec3 offset{0.0f};   // pivot, model units
    glm::vec3 rotation{0.0f}; // radians; vanilla applies Z, then Y, then X (rotationZYX)
    glm::vec3 scale{1.0f};
};

struct ModelPart {
    std::string name;
    std::vector<ModelCube> cubes;
    PartPose pose;
    std::vector<ModelPart> children;
    bool visible = true;

    ModelPart* find(std::string_view path);             // "head/hat"
    const ModelPart* find(std::string_view path) const;
    ModelPart& addChild(std::string childName, PartPose childPose = {});
    // Vanilla CubeListBuilder.addBox(x, y, z, w, h, d) at `texOffs`.
    ModelPart& box(glm::vec2 texOffs, glm::vec3 from, glm::vec3 size, float grow = 0.0f, bool mirror = false);
    // Keeps only the faces in `mask` (bit per Direction) of the last box, like addBox's visibleFaces.
    ModelPart& onlyFaces(uint8_t mask);
};

struct ModelDefinition {
    ModelPart root;
    int texWidth = 64, texHeight = 64;
};

struct ModelQuad {
    std::array<glm::vec3, 4> pos; // root space, blocks (model units / 16)
    std::array<glm::vec2, 4> uv;  // 0..1 across the model's texture
    glm::vec3 normal;
};

// The local transform of a pose (translate pivot, rotate Z*Y*X, scale), in blocks.
glm::mat4 poseMatrix(const PartPose& pose);
// Bakes every visible cube of `part` and its children, poses applied on top of `transform`.
void bakeModel(const ModelPart& part, const glm::mat4& transform, int texWidth, int texHeight,
               std::vector<ModelQuad>& out);

// Texel rectangles [u0, v0, u1, v1) the cubes' faces read, for checking that a
// definition covers its texture (the UV-island test).
std::vector<glm::ivec4> modelUvRects(const ModelDefinition& model);

} // namespace mc
