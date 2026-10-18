#include <cmath>

#include "world/block/BlockRegistry.h"
#include "world/gen/ChunkGenerator.h"

namespace mc {

FlatGenerator::FlatGenerator(const BlockRegistry& r)
    : FlatGenerator(r, {{r.defaultState("bedrock"), 1}, {r.defaultState("dirt"), 2}, {r.defaultState("grass_block"), 1}}) {}

FlatGenerator::FlatGenerator(const BlockRegistry&, std::vector<Layer> layers) : layers_(std::move(layers)) {}

void FlatGenerator::generateTerrain(Chunk& chunk) const {
    for (Section& s : chunk.sections) s.biomes.fill(biome);
    int y = kWorldMinY;
    for (const Layer& l : layers_)
        for (int i = 0; i < l.count && y <= kWorldMaxY; ++i, ++y)
            for (int z = 0; z < 16; ++z)
                for (int x = 0; x < 16; ++x) chunk.set(x, y, z, l.state);
}

glm::dvec3 FlatGenerator::spawnPoint() const {
    int height = 0;
    for (const Layer& l : layers_) height += l.count;
    return {0.5, double(kWorldMinY + height), 0.5};
}

DebugGenerator::DebugGenerator(const BlockRegistry& r) {
    for (size_t s = 1; s < r.stateCount(); ++s) {
        const StateId id = StateId(s);
        if (r.info(id).flags & kAir) continue;
        states_.push_back(id);
    }
    width_ = int(std::ceil(std::sqrt(double(states_.size()))));
    height_ = int(std::ceil(double(states_.size()) / double(width_)));
    barrier_ = r.defaultState("barrier");
}

StateId DebugGenerator::stateAt(int x, int z) const {
    if (x <= 0 || z <= 0 || x % 2 == 0 || z % 2 == 0) return 0;
    x /= 2;
    z /= 2;
    if (x > width_ || z > height_) return 0;
    const size_t i = size_t(x) * size_t(width_) + size_t(z);
    return i < states_.size() ? states_[i] : StateId(0);
}

void DebugGenerator::generateTerrain(Chunk& chunk) const {
    const int bx = chunk.pos.minBlockX(), bz = chunk.pos.minBlockZ();
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) {
            chunk.set(x, 60, z, barrier_);
            if (const StateId s = stateAt(bx + x, bz + z)) chunk.set(x, 70, z, s);
        }
}

} // namespace mc
