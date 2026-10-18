// ChunkMesher.h -- builds section meshes (16-byte vertices) from chunk snapshots.
// GL-free: runs on worker threads and in unit tests.
#pragma once

#include <memory>
#include <unordered_set>

#include <array>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "world/block/BlockRegistry.h"
#include "world/chunk/ChunkSnapshot.h"
#include "world/mesh/VisGraph.h"

namespace mc {

class Atlas;
class BiomeRegistry;
class BlockEntityMeshes;

// 16 bytes. Position is section-local in 1/2048 block units offset by 8 blocks,
// so models may reach from -8 to +24 blocks around the section origin.
struct TerrainVertex {
    uint16_t x, y, z;
    uint8_t blockLight, skyLight; // light level * 16 (smoothly interpolated)
    uint16_t u, v;                // atlas UV, unorm16
    uint8_t r, g, b;              // tint * directional shade * ambient occlusion
    uint8_t flags;
};
static_assert(sizeof(TerrainVertex) == 16);

constexpr int kLayerCount = 4; // Solid, CutoutMipped, Cutout, Translucent (RenderLayer order)

struct SectionMesh {
    int section = 0; // section index 0..23
    std::array<std::vector<TerrainVertex>, kLayerCount> layers;
    // Translucent quad centers (section-local), one per quad, for back-to-front sorting.
    std::vector<glm::vec3> translucentCenters;
    // Which faces see each other through the section (all, for an empty one): cave culling.
    VisibilitySet visibility;
    bool empty() const {
        for (const auto& l : layers)
            if (!l.empty()) return false;
        return true;
    }
    size_t quadCount() const {
        size_t n = 0;
        for (const auto& l : layers) n += l.size() / 4;
        return n;
    }
};

// Atlas sprites the fluid renderer needs (indices into the block atlas).
struct FluidSprites {
    struct Sprite {
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        float u(float t) const { return u0 + (u1 - u0) * t; } // t in 0..1
        float v(float t) const { return v0 + (v1 - v0) * t; }
    };
    Sprite waterStill, waterFlow, waterOverlay, lavaStill, lavaFlow;
};

struct MesherOptions {
    bool smoothLighting = true;
    bool fastLeaves = false; // Fast graphics: leaves are solid (their see-through texels opaque)
    BiomeColors colors;                  // used when `biomes` is null
    const BiomeRegistry* biomes = nullptr; // per-position biome colors
    int biomeBlend = 2;                  // blend radius in blocks (vanilla default 2 = 5x5)
    FluidSprites fluids;
    glm::vec3 cameraPos{0.0f};           // world position, for the initial translucent sort
    const BlockEntityMeshes* blockEntities = nullptr; // chests, beds, signs... (null: not drawn)
    // Block entities drawn by the entity renderer for now (chests opening): left out.
    std::shared_ptr<const std::unordered_set<BlockPos>> dynamicBlockEntities;
};

struct MesherStats {
    size_t blocks = 0, quads = 0, culled = 0;
};

// Vanilla's directional block shading (getShade): up 1, down 0.5, north/south 0.8, east/west 0.6.
float blockFaceShade(Direction d);

// Reorders a translucent layer back to front as seen from `eye` (section-local).
void sortTranslucentQuads(std::vector<TerrainVertex>& vertices, std::vector<glm::vec3>& centers, const glm::vec3& eye);

// Meshes the sections of the center chunk of `n` selected by `sectionMask`.
// Every requested section produces an entry (possibly empty) so stale meshes get cleared.
void meshSections(const BlockRegistry& registry, const MesherOptions& options, const Neighborhood& n,
                  uint32_t sectionMask, std::vector<SectionMesh>& out, MesherStats* stats = nullptr);

} // namespace mc
