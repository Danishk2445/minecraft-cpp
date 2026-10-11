// ModelBaker.h -- turns resolved model JSON + blockstate rotation into quads
// (vanilla FaceBakery semantics; see docs/DESIGN.md "Model baking").
#pragma once

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "core/Math.h"
#include "resources/Atlas.h"
#include "resources/BlockStateDef.h"
#include "resources/Model.h"

namespace mc {

struct BakedQuad {
    std::array<glm::vec3, 4> pos; // block units (a full cube spans 0..1)
    std::array<glm::vec2, 4> uv;  // atlas coordinates
    Direction face = Direction::Up; // nearest direction of the quad normal
    int8_t cull = -1;               // rotated cullface (Direction index) or -1
    int8_t tint = -1;
    bool shade = true;
    uint8_t emission = 0;
    uint16_t sprite = 0;
    // 4x4 cells of the quad's face plane it overlaps (bit = row * 4 + col, see
    // faceCellMask); the mesher culls the quad when the neighbor covers them all.
    uint16_t cullMask = 0;
    // The quad lies on the block boundary in its facing direction (smooth light
    // samples the neighbor cell for these).
    bool flush = false;
};

// Cells of a 4x4 grid on face plane `d` touched (overlap = true) or fully
// contained (overlap = false) by the rectangle spanned by `pos` (block units).
uint16_t faceCellMask(Direction d, const std::array<glm::vec3, 4>& pos, bool overlap);

struct BakedModel {
    std::vector<BakedQuad> quads; // model order: overlays follow their base quads
    bool ambientOcclusion = true;
    bool guiLightFront = false;
    bool builtinGenerated = false;
    uint16_t particleSprite = 0;
    std::array<ItemTransform, size_t(DisplayContext::Count)> display{};
    std::array<bool, size_t(DisplayContext::Count)> hasDisplay{};
};

// R = RotY(-y) * RotX(-x): y=90 maps north to east, x=90 maps up to north.
glm::mat3 blockStateRotation(int xDegrees, int yDegrees);

// Texture axes (u, v) of each face in model space, such that the automatic UV
// of a point is 8 + axis . (p - 8).
glm::vec3 faceUAxis(Direction d);
glm::vec3 faceVAxis(Direction d);

// UV rectangle [u0, v0, u1, v1] vanilla derives when a face has no "uv".
std::array<float, 4> autoFaceUV(Direction d, const glm::vec3& from, const glm::vec3& to);

// Bakes one element face. `rotation` is the blockstate rotation; with `uvlock`
// the texture stays aligned to the world axes. Returns false for zero-area faces.
bool bakeFace(const ElementDef& element, Direction dir, const FaceDef& face, const SpriteInfo& sprite,
              const glm::mat3& rotation, bool uvlock, BakedQuad& out, int spriteIndex = 0);

class ModelBaker {
public:
    ModelBaker(ModelStore& models, const Atlas& atlas);

    // Cached by (model, x, y, uvlock). Missing models bake to a magenta cube.
    const BakedModel* bake(const ModelVariant& variant);

    const std::vector<std::string>& errors() const { return errors_; }
    int missingTextureCount() const { return missingTextures_; }
    size_t bakedCount() const { return cache_.size(); }

private:
    std::unique_ptr<BakedModel> bakeUncached(const ModelVariant& v);
    uint16_t spriteFor(const ResolvedModel& model, const std::string& variable);

    ModelStore& models_;
    const Atlas& atlas_;
    std::unordered_map<std::string, std::unique_ptr<BakedModel>> cache_;
    std::vector<std::string> errors_;
    int missingTextures_ = 0;
};

} // namespace mc
