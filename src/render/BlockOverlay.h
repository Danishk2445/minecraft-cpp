// BlockOverlay.h -- the targeted block's selection outline and breaking cracks.
#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "gfx/Shader.h"
#include "world/block/BlockRegistry.h"

namespace mc {

class ClientAssets;
struct Camera;

class BlockOverlay {
public:
    ~BlockOverlay();
    bool init(const ClientAssets& assets);

    // Outline of `shape` at `pos` (vanilla: black at 40%, a little over 2 px wide),
    // plus cracks at `crackStage` 0..9 when breaking (-1: none).
    void render(const Camera& camera, glm::ivec2 viewport, const BlockPos& pos, const Shape& shape, int crackStage);

private:
    struct LineVertex {
        glm::vec3 pos, other;
        float side;
    };
    struct CrackVertex {
        glm::vec3 pos;
        glm::vec2 uv;
    };

    const ClientAssets* assets_ = nullptr;
    Shader lines_, crack_;
    unsigned lineVao_ = 0, lineVbo_ = 0, crackVao_ = 0, crackVbo_ = 0;
    std::vector<LineVertex> lineVerts_;
    std::vector<CrackVertex> crackVerts_;
    int crackSprites_[10] = {};
};

} // namespace mc
