#include "world/level/Level.h"

namespace mc {

Level::Level(const BlockRegistry& registry, std::shared_ptr<const ChunkGenerator> generator, JobSystem& jobs,
             int64_t seed)
    : registry_(registry), chunks_(registry, std::move(generator), jobs), seed_(seed) {}

void Level::tick() {
    ++gameTime_;
    if (daylightCycle) ++dayTime_;
}

} // namespace mc
