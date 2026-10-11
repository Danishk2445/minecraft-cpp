// Level.h -- one dimension's world state: chunks, time, and block access.
#pragma once

#include "core/Json.h"
#include "world/block/BlockGetter.h"

#include <cstdint>
#include <memory>
#include <vector>

#include "world/chunk/ChunkManager.h"

namespace mc {

class ChunkStorage;

class Level final : public BlockGetter {
public:
    static constexpr int64_t kDayLength = 24000;

    Level(const BlockRegistry& registry, std::shared_ptr<const ChunkGenerator> generator, JobSystem& jobs,
          int64_t seed);

    ChunkManager& chunks() { return chunks_; }
    const ChunkManager& chunks() const { return chunks_; }
    const BlockRegistry& registry() const override { return registry_; }
    const ChunkGenerator& generator() const { return chunks_.generator(); }
    int64_t seed() const { return seed_; }

    StateId getBlock(const BlockPos& p) const override { return chunks_.getBlock(p); }
    // Packed light (sky << 4 | block) at p; full sky light where no chunk is loaded.
    uint8_t lightAt(const BlockPos& p) const;
    // False when the target chunk is not loaded and ready.
    bool setBlock(const BlockPos& p, StateId state) { return chunks_.setBlock(p, state); }
    // setBlock, then the neighbors react (vanilla updateShape: connections, halves,
    // support), cascading up to vanilla's 512 updates.
    bool setBlockAndUpdate(const BlockPos& p, StateId state);

    void tick();

    // Saves edited chunks (with their scheduled ticks) through `storage`, and loads
    // saved chunks in place of generated ones.
    void attachStorage(std::shared_ptr<const ChunkStorage> storage);
    // Level-wide state for level.json: times and the daylight cycle.
    Json saveData() const;
    void loadData(const Json& j);

    // Vanilla scheduled block ticks: `block` at `p` gets ticked after `delay` game
    // ticks (if it is still there then).
    void scheduleTick(const BlockPos& p, uint16_t block, int delay);
    size_t pendingTicks() const { return scheduled_.size(); }

    int64_t gameTime() const { return gameTime_; }
    int64_t dayTime() const { return dayTime_; }
    void setDayTime(int64_t t) { dayTime_ = t; }
    bool daylightCycle = true;

private:
    const BlockRegistry& registry_;
    ChunkManager chunks_;
    int64_t seed_;
    struct ScheduledTick {
        int64_t time;
        uint64_t seq;
        BlockPos pos;
        uint16_t block;
        bool operator>(const ScheduledTick& o) const { return time != o.time ? time > o.time : seq > o.seq; }
    };
    void runScheduledTicks();

    std::vector<ScheduledTick> scheduled_; // min-heap by (time, seq)
    uint64_t tickSeq_ = 0;
    int64_t gameTime_ = 0;
    int64_t dayTime_ = 6000; // noon
};

} // namespace mc
