// Level.h -- one dimension's world state: chunks, time, and block access.
#pragma once

#include <cstdint>
#include <memory>

#include "world/chunk/ChunkManager.h"

namespace mc {

class Level {
public:
    static constexpr int64_t kDayLength = 24000;

    Level(const BlockRegistry& registry, std::shared_ptr<const ChunkGenerator> generator, JobSystem& jobs,
          int64_t seed);

    ChunkManager& chunks() { return chunks_; }
    const ChunkManager& chunks() const { return chunks_; }
    const BlockRegistry& registry() const { return registry_; }
    const ChunkGenerator& generator() const { return chunks_.generator(); }
    int64_t seed() const { return seed_; }

    StateId getBlock(const BlockPos& p) const { return chunks_.getBlock(p); }
    // False when the target chunk is not loaded and ready.
    bool setBlock(const BlockPos& p, StateId state) { return chunks_.setBlock(p, state); }

    void tick();

    int64_t gameTime() const { return gameTime_; }
    int64_t dayTime() const { return dayTime_; }
    void setDayTime(int64_t t) { dayTime_ = t; }
    bool daylightCycle = true;

private:
    const BlockRegistry& registry_;
    ChunkManager chunks_;
    int64_t seed_;
    int64_t gameTime_ = 0;
    int64_t dayTime_ = 6000; // noon
};

} // namespace mc
