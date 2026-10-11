#include "world/gen/feature/DecorationRegion.h"

#include <algorithm>

namespace mc {

bool countsForHeightmap(const BlockRegistry& reg, TagId leaves, Heightmap type, StateId s) {
    const StateInfo& i = reg.info(s);
    switch (type) {
    case Heightmap::WorldSurface: return !(i.flags & kAir);
    case Heightmap::OceanFloor: return (i.flags & kBlocksMotion) != 0;
    case Heightmap::MotionBlocking: return (i.flags & kBlocksMotion) || !i.fluid.empty();
    case Heightmap::MotionBlockingNoLeaves:
        return ((i.flags & kBlocksMotion) || !i.fluid.empty()) && !reg.hasTag(s, leaves);
    }
    return false;
}

DecorationRegion::DecorationRegion(const BlockRegistry& registry, const BiomeRegistry& biomes,
                                   const TerrainNeighborhood& terrain, int64_t seed, DecorationResult& out)
    : reg_(registry), biomes_(biomes), terrain_(terrain), seed_(seed), out_(out) {
    leaves_ = registry.tag("leaves").value_or(TagId(0));
    x0_ = (terrain.center.x - 1) * 16;
    z0_ = (terrain.center.z - 1) * 16;
    heights_.fill(kUnknown);
    chunkTop_.fill(kUnknown);
    overlayTop_.fill(kUnknown);
}

int DecorationRegion::sectionSlot(const BlockPos& p) const {
    const int cx = (p.x >> 4) - terrain_.center.x + 1, cz = (p.z >> 4) - terrain_.center.z + 1;
    if (cx < 0 || cx > 2 || cz < 0 || cz > 2 || !inBuildHeight(p.y)) return -1;
    return (cz * 3 + cx) * kSectionCount + sectionIndexOfY(p.y);
}

const Chunk* DecorationRegion::chunkAt(int x, int z) const { return terrain_.chunkAt(ChunkPos(x >> 4, z >> 4)); }

StateId DecorationRegion::terrainBlock(const BlockPos& p) const {
    const Chunk* c = chunkAt(p.x, p.z);
    return c ? c->get(p.x & 15, p.y, p.z & 15) : StateId(0);
}

StateId DecorationRegion::getBlock(const BlockPos& p) const {
    const int slot = sectionSlot(p);
    if (slot >= 0 && overlay_[size_t(slot)]) {
        const StateId s = (*overlay_[size_t(slot)])[size_t(SectionBlocks::index(p.x & 15, (p.y - kWorldMinY) & 15, p.z & 15))];
        if (s != kNoOverlay) return s;
    }
    return terrainBlock(p);
}

bool DecorationRegion::canWrite(const BlockPos& p) const {
    return inBuildHeight(p.y) && columnIndex(p.x, p.z) >= 0 && chunkAt(p.x, p.z) != nullptr;
}

int DecorationRegion::columnIndex(int x, int z) const {
    const int lx = x - x0_, lz = z - z0_;
    if (lx < 0 || lz < 0 || lx >= 48 || lz >= 48) return -1;
    return lz * 48 + lx;
}

bool DecorationRegion::setBlock(const BlockPos& p, StateId s, WritePolicy policy) {
    if (!canWrite(p)) return false;
    const StateId before = getBlock(p);
    out_.writes.push_back({p, s, before, feature_, step_, uint8_t(policy)});
    auto& section = overlay_[size_t(sectionSlot(p))];
    if (!section) {
        section = std::make_unique<SectionOverlay>();
        section->fill(kNoOverlay);
    }
    (*section)[size_t(SectionBlocks::index(p.x & 15, (p.y - kWorldMinY) & 15, p.z & 15))] = s;
    // Keep cached heightmaps of this column current.
    const int col = columnIndex(p.x, p.z);
    overlayTop_[size_t(col)] = std::max<int16_t>(overlayTop_[size_t(col)], int16_t(p.y));
    for (int t = 0; t < kHeightmapCount; ++t) {
        int16_t& h = heights_[size_t(col * kHeightmapCount + t)];
        if (h == kUnknown) continue;
        const bool counts = countsForHeightmap(reg_, leaves_, Heightmap(t), s);
        if (counts && p.y >= h) h = int16_t(p.y + 1);
        else if (!counts && p.y == h - 1) h = kUnknown; // the top block went away: rescan later
    }
    return true;
}

int DecorationRegion::computeHeight(Heightmap type, int x, int z) const {
    const Chunk* c = chunkAt(x, z);
    if (!c) return kWorldMinY;
    const size_t ci = size_t(((z >> 4) - terrain_.center.z + 1) * 3 + ((x >> 4) - terrain_.center.x + 1));
    if (chunkTop_[ci] == kUnknown) chunkTop_[ci] = int16_t(c->topY());
    for (int y = std::min<int>(chunkTop_[ci], kWorldMaxY); y >= kWorldMinY; --y)
        if (countsForHeightmap(reg_, leaves_, type, getBlock({x, y, z}))) return y + 1;
    return kWorldMinY;
}

int DecorationRegion::height(Heightmap type, int x, int z) const {
    const int col = columnIndex(x, z);
    if (col < 0) return kWorldMinY;
    int16_t& h = heights_[size_t(col * kHeightmapCount + int(type))];
    if (h == kUnknown) {
        // computeHeight scans down from the chunk's original top; writes above it
        // (tall trees) are only in the overlay.
        int top = computeHeight(type, x, z);
        for (int y = overlayTop_[size_t(col)]; y >= top; --y)
            if (countsForHeightmap(reg_, leaves_, type, getBlock({x, y, z}))) {
                top = y + 1;
                break;
            }
        h = int16_t(top);
    }
    return h;
}

BiomeId DecorationRegion::biome(const BlockPos& p) const {
    const Chunk* c = chunkAt(p.x, p.z);
    if (!c) return 0;
    const int y = std::clamp(p.y, kWorldMinY, kWorldMaxY);
    const Section& s = c->sections[size_t(sectionIndexOfY(y))];
    return s.biomes[size_t(((((y - kWorldMinY) & 15) >> 2) << 4) | (((p.z & 15) >> 2) << 2) | ((p.x & 15) >> 2))];
}

} // namespace mc
