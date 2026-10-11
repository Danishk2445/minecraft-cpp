// Chunk.h -- 16x384x16 columns of 24 sections with copy-on-write storage.
//
// Section blocks and light are shared_ptrs. Worker jobs take shared_ptr<const ...>
// copies as immutable snapshots; the main thread clones a section before writing
// to it whenever anyone else still holds it, so jobs never observe a write.
#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "core/Math.h"
#include "world/block/BlockRegistry.h"

namespace mc {

constexpr int kWorldMinY = -64;
constexpr int kWorldHeight = 384;
constexpr int kWorldMaxY = kWorldMinY + kWorldHeight - 1; // 319
constexpr int kSectionCount = kWorldHeight / 16;          // 24
constexpr int kSectionVolume = 4096;

constexpr int sectionIndexOfY(int y) { return (y - kWorldMinY) >> 4; }
constexpr int sectionMinY(int sectionIndex) { return kWorldMinY + sectionIndex * 16; }

struct SectionBlocks {
    std::array<StateId, kSectionVolume> states{};
    uint16_t nonAir = 0; // states other than StateId 0
    static constexpr int index(int x, int y, int z) { return (y << 8) | (z << 4) | x; }
};

// One byte per cell: sky light in the high nibble, block light in the low nibble.
struct SectionLight {
    std::array<uint8_t, kSectionVolume> values{};
};

constexpr uint8_t packLight(int sky, int block) { return uint8_t((sky << 4) | block); }

struct Section {
    std::shared_ptr<SectionBlocks> blocks; // null: all air
    std::shared_ptr<SectionLight> light;   // null: every cell is `uniformLight`
    uint8_t uniformLight = packLight(15, 0);
    std::array<uint8_t, 64> biomes{}; // 4x4x4 cells, index (y<<4)|(z<<2)|x

    StateId get(int x, int y, int z) const { return blocks ? blocks->states[size_t(SectionBlocks::index(x, y, z))] : 0; }
    uint8_t lightAt(int x, int y, int z) const {
        return light ? light->values[size_t(SectionBlocks::index(x, y, z))] : uniformLight;
    }
};

class Chunk {
public:
    explicit Chunk(ChunkPos p) : pos(p) {}

    ChunkPos pos;
    std::array<Section, kSectionCount> sections;
    uint32_t blockVersion = 0; // bumped by every block change
    uint32_t lightVersion = 0; // bumped by every light change

    // Local x/z in 0..15, world y. Out-of-range y reads as air.
    StateId get(int lx, int y, int lz) const {
        if (y < kWorldMinY || y > kWorldMaxY) return 0;
        return sections[size_t(sectionIndexOfY(y))].get(lx, (y - kWorldMinY) & 15, lz);
    }
    uint8_t lightAt(int lx, int y, int lz) const {
        if (y < kWorldMinY) return packLight(0, 0);
        if (y > kWorldMaxY) return packLight(15, 0);
        return sections[size_t(sectionIndexOfY(y))].lightAt(lx, (y - kWorldMinY) & 15, lz);
    }

    // Copy-on-write block write; returns the previous state. Does not bump versions
    // (Level does, so batched edits count once).
    StateId set(int lx, int y, int lz, StateId state);
    void setLight(int lx, int y, int lz, uint8_t packed);

    // Writable section storage (cloned first if shared with a snapshot).
    SectionBlocks& mutableBlocks(int sectionIndex);
    SectionLight& mutableLight(int sectionIndex);

    // Highest non-air y + 1 in the chunk (kWorldMinY if empty).
    int topY() const;
    // True if every section of `other` shares storage with this chunk's.
    bool sameStorage(const Chunk& other) const;
};

} // namespace mc
