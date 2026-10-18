#include "world/sim/LiveRegion.h"

#include <algorithm>

#include "world/gen/feature/BiomeDecorator.h"
#include "world/level/Level.h"

namespace mc::sim {

LiveRegion::LiveRegion(Level& level, const BiomeRegistry& biomes) : level_(level), biomes_(biomes) {
    leaves_ = level.registry().tag("leaves").value_or(TagId(0));
}

LiveRegion::~LiveRegion() { finish(); }

const BlockRegistry& LiveRegion::registry() const { return level_.registry(); }

StateId LiveRegion::getBlock(const BlockPos& p) const { return level_.getBlock(p); }

bool LiveRegion::canWrite(const BlockPos& p) const {
    return inBuildHeight(p.y) && level_.chunks().isReady(ChunkPos::of(p));
}

bool LiveRegion::setBlock(const BlockPos& p, StateId s, WritePolicy) {
    if (!canWrite(p)) return false;
    const StateId old = level_.getBlock(p);
    if (old == s) return true;
    if (!level_.setBlock(p, s)) return false;
    changes_.push_back({p, old});
    return true;
}

int LiveRegion::height(Heightmap type, int x, int z) const {
    const Chunk* c = level_.chunks().chunk(ChunkPos::of(BlockPos(x, 0, z)));
    if (!c) return kWorldMinY;
    // Above the chunk's original top only what was just written can count.
    int top = std::min(c->topY(), kWorldMaxY);
    for (const Change& ch : changes_)
        if (ch.pos.x == x && ch.pos.z == z) top = std::max(top, ch.pos.y);
    for (int y = top; y >= kWorldMinY; --y)
        if (countsForHeightmap(level_.registry(), leaves_, type, level_.getBlock({x, y, z}))) return y + 1;
    return kWorldMinY;
}

BiomeId LiveRegion::biome(const BlockPos& p) const { return level_.biomeAt(p); }

int64_t LiveRegion::seed() const { return level_.seed(); }

void LiveRegion::finish() {
    // In the order written, each block's updates as if set with setBlockAndUpdate (vanilla
    // features set with flags that notify neighbors).
    const std::vector<Change> changes = std::exchange(changes_, {});
    for (const Change& ch : changes) {
        const StateId now = level_.getBlock(ch.pos);
        if (now != ch.old) level_.updateAround(ch.pos, ch.old, now);
    }
}

bool placeFeature(Level& level, std::string_view name, const BlockPos& origin) {
    if (!level.decorator() || !level.biomes()) return false;
    const PlacedFeature* placed = level.decorator()->features().find(name);
    if (!placed || !placed->feature) return false;
    LiveRegion region(level, *level.biomes());
    PlacementContext placement{region};
    FeatureContext ctx{region, level.random(), origin, placement};
    const bool ok = placed->feature->place(ctx);
    region.finish();
    return ok;
}

} // namespace mc::sim
