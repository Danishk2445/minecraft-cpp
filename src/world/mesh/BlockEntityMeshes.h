// BlockEntityMeshes.h -- block-entity models (chests, beds, signs, ...) baked once
// per block state into quads the chunk mesher emits like model quads, with
// textures in the block atlas and vanilla's entity lighting.
#pragma once

#include <array>
#include <vector>

#include <glm/glm.hpp>

#include "world/block/BlockRegistry.h"

namespace mc {

class Atlas;
class BlockEntityModels;

struct EntityQuad {
    std::array<glm::vec3, 4> pos; // block-local
    std::array<glm::vec2, 4> uv;  // atlas
    glm::vec3 color;              // tint * entity shading, 0..255
};

class BlockEntityMeshes {
public:
    void build(const BlockRegistry& reg, const BlockEntityModels& models, const Atlas& atlas);
    // Quads for `state`, or null when it is not a block entity.
    const std::vector<EntityQuad>* quads(StateId state) const {
        const int i = state < index_.size() ? index_[state] : -1;
        return i < 0 ? nullptr : &lists_[size_t(i)];
    }

private:
    std::vector<int> index_; // per state -> lists_ (-1: none)
    std::vector<std::vector<EntityQuad>> lists_;
};

// Vanilla entity lighting: two directional lights plus ambient, as a brightness factor.
float entityShade(const glm::vec3& normal);

} // namespace mc
