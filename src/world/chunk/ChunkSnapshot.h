// ChunkSnapshot.h -- immutable views of chunk columns for worker jobs (light, meshing).
#pragma once

#include <array>
#include <memory>

#include "world/chunk/Chunk.h"

namespace mc {

struct ColumnSnapshot {
    bool valid = false;
    std::array<std::shared_ptr<const SectionBlocks>, kSectionCount> blocks;
    std::array<std::shared_ptr<const SectionLight>, kSectionCount> light;
    std::array<uint8_t, kSectionCount> uniformLight{};
    std::array<std::array<uint8_t, 64>, kSectionCount> biomes{};
    uint32_t blockVersion = 0;
    uint32_t lightVersion = 0;

    static ColumnSnapshot of(const Chunk& c) {
        ColumnSnapshot s;
        s.valid = true;
        for (int i = 0; i < kSectionCount; ++i) {
            s.blocks[size_t(i)] = c.sections[size_t(i)].blocks;
            s.light[size_t(i)] = c.sections[size_t(i)].light;
            s.uniformLight[size_t(i)] = c.sections[size_t(i)].uniformLight;
            s.biomes[size_t(i)] = c.sections[size_t(i)].biomes;
        }
        s.blockVersion = c.blockVersion;
        s.lightVersion = c.lightVersion;
        return s;
    }

    // Local x/z 0..15, world y.
    StateId get(int lx, int y, int lz) const {
        if (y < kWorldMinY || y > kWorldMaxY) return 0;
        const auto& b = blocks[size_t(sectionIndexOfY(y))];
        return b ? b->states[size_t(SectionBlocks::index(lx, (y - kWorldMinY) & 15, lz))] : StateId(0);
    }
    uint8_t lightAt(int lx, int y, int lz) const {
        if (y < kWorldMinY) return packLight(0, 0);
        if (y > kWorldMaxY) return packLight(15, 0);
        const int si = sectionIndexOfY(y);
        const auto& l = light[size_t(si)];
        return l ? l->values[size_t(SectionBlocks::index(lx, (y - kWorldMinY) & 15, lz))] : uniformLight[size_t(si)];
    }
};

// A chunk and its 8 neighbors, index (dz+1)*3+(dx+1).
struct Neighborhood {
    ChunkPos center;
    std::array<ColumnSnapshot, 9> cols;

    const ColumnSnapshot* column(int dx, int dz) const {
        if (dx < -1 || dx > 1 || dz < -1 || dz > 1) return nullptr;
        const ColumnSnapshot& c = cols[size_t((dz + 1) * 3 + dx + 1)];
        return c.valid ? &c : nullptr;
    }
    // Coordinates relative to the center chunk's minimum corner (x/z in -16..31).
    StateId get(int x, int y, int z) const {
        const ColumnSnapshot* c = column(floorDiv(x, 16), floorDiv(z, 16));
        return c ? c->get(x & 15, y, z & 15) : StateId(0);
    }
    uint8_t lightAt(int x, int y, int z) const {
        const ColumnSnapshot* c = column(floorDiv(x, 16), floorDiv(z, 16));
        return c ? c->lightAt(x & 15, y, z & 15) : packLight(15, 0);
    }
};

} // namespace mc
