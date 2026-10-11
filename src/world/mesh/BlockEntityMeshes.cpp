#include "world/mesh/BlockEntityMeshes.h"

#include <algorithm>

#include "entity/model/BlockEntityModels.h"
#include "resources/Atlas.h"

namespace mc {

float entityShade(const glm::vec3& normal) {
    // Lighting.setupLevel: light 0 (0.2, 1, -0.7), light 1 (-0.2, 1, 0.7); ambient 0.4, diffuse 0.6.
    static const glm::vec3 l0 = glm::normalize(glm::vec3(0.2f, 1.0f, -0.7f));
    static const glm::vec3 l1 = glm::normalize(glm::vec3(-0.2f, 1.0f, 0.7f));
    const float d = std::max(0.0f, glm::dot(normal, l0)) + std::max(0.0f, glm::dot(normal, l1));
    return std::min(1.0f, 0.4f + 0.6f * d);
}

void BlockEntityMeshes::build(const BlockRegistry& reg, const BlockEntityModels& models, const Atlas& atlas) {
    index_.assign(reg.stateCount(), -1);
    lists_.clear();
    std::vector<BlockEntityPiece> pieces;
    std::vector<ModelQuad> baked;
    for (size_t b = 0; b < reg.blockCount(); ++b) {
        const Block& block = reg.block(b);
        if (!models.handles(block)) continue;
        for (uint32_t local = 0; local < block.stateCount; ++local) {
            const StateId s = block.stateAt(local);
            pieces.clear();
            models.pieces(reg, s, pieces);
            if (pieces.empty()) continue;
            std::vector<EntityQuad> quads;
            for (const BlockEntityPiece& piece : pieces) {
                const SpriteInfo& sprite = atlas.sprite(piece.texture);
                baked.clear();
                bakeModel(piece.model->root, piece.transform, piece.model->texWidth, piece.model->texHeight, baked);
                const glm::vec3 tint(float((piece.tint >> 16) & 255), float((piece.tint >> 8) & 255), float(piece.tint & 255));
                for (const ModelQuad& q : baked) {
                    EntityQuad e;
                    for (int k = 0; k < 4; ++k) {
                        e.pos[size_t(k)] = q.pos[size_t(k)];
                        e.uv[size_t(k)] = {sprite.u0 + (sprite.u1 - sprite.u0) * q.uv[size_t(k)].x,
                                           sprite.v0 + (sprite.v1 - sprite.v0) * q.uv[size_t(k)].y};
                    }
                    e.color = tint * entityShade(q.normal);
                    quads.push_back(e);
                    if (piece.doubleSided) {
                        // The terrain pass culls back faces; give flat parts their other side.
                        std::reverse(e.pos.begin(), e.pos.end());
                        std::reverse(e.uv.begin(), e.uv.end());
                        e.color = tint * entityShade(-q.normal);
                        quads.push_back(e);
                    }
                }
            }
            index_[s] = int(lists_.size());
            lists_.push_back(std::move(quads));
        }
    }
}

} // namespace mc
