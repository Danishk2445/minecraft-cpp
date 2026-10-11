// ChunkMesher.h -- builds section meshes (16-byte vertices) from chunk snapshots.
// GL-free: runs on worker threads and in unit tests.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "world/block/BlockRegistry.h"
#include "world/chunk/ChunkSnapshot.h"

namespace mc {

class Atlas;

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

struct MesherOptions {
    bool smoothLighting = true;
    BiomeColors colors; // until biomes exist (M4)
};

struct MesherStats {
    size_t blocks = 0, quads = 0, culled = 0;
};

// Meshes the sections of the center chunk of `n` selected by `sectionMask`.
// Every requested section produces an entry (possibly empty) so stale meshes get cleared.
void meshSections(const BlockRegistry& registry, const MesherOptions& options, const Neighborhood& n,
                  uint32_t sectionMask, std::vector<SectionMesh>& out, MesherStats* stats = nullptr);

} // namespace mc
