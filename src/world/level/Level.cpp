#include "world/level/Level.h"

#include <algorithm>
#include <functional>

#include "world/block/BlockBehavior.h"

namespace mc {

Level::Level(const BlockRegistry& registry, std::shared_ptr<const ChunkGenerator> generator, JobSystem& jobs,
             int64_t seed)
    : registry_(registry), chunks_(registry, std::move(generator), jobs), seed_(seed) {}

void Level::tick() {
    ++gameTime_;
    if (daylightCycle) ++dayTime_;
    runScheduledTicks();
}

void Level::scheduleTick(const BlockPos& p, uint16_t block, int delay) {
    scheduled_.push_back({gameTime_ + std::max(delay, 1), tickSeq_++, p, block});
    std::push_heap(scheduled_.begin(), scheduled_.end(), std::greater<>());
}

void Level::runScheduledTicks() {
    std::vector<ScheduledTick> waiting; // due, but their chunk is not ticking yet
    for (int budget = 65536; budget > 0 && !scheduled_.empty() && scheduled_.front().time <= gameTime_; --budget) {
        std::pop_heap(scheduled_.begin(), scheduled_.end(), std::greater<>());
        const ScheduledTick t = scheduled_.back();
        scheduled_.pop_back();
        if (!chunks_.isReady(ChunkPos::of(t.pos))) {
            waiting.push_back(t);
            continue;
        }
        const StateId s = getBlock(t.pos);
        if (registry_.info(s).block != t.block) continue; // the block changed meanwhile
        const StateId after = scheduledTickResult(registry_, *this, s, t.pos);
        if (after != s) setBlockAndUpdate(t.pos, after);
    }
    for (ScheduledTick& t : waiting) {
        t.time = gameTime_ + 1;
        scheduled_.push_back(t);
        std::push_heap(scheduled_.begin(), scheduled_.end(), std::greater<>());
    }
}

uint8_t Level::lightAt(const BlockPos& p) const {
    const Chunk* c = chunks_.chunk(ChunkPos::of(p));
    return c ? c->lightAt(p.x & 15, p.y, p.z & 15) : packLight(15, 0);
}

bool Level::setBlockAndUpdate(const BlockPos& p, StateId state) {
    if (!setBlock(p, state)) return false;
    std::vector<BlockPos> pending{p};
    for (int budget = 512; !pending.empty() && budget > 0;) {
        const BlockPos q = pending.back();
        pending.pop_back();
        const StateId qs = getBlock(q);
        for (Direction d : kDirections) {
            const BlockPos n = q.offset(d);
            const StateId ns = getBlock(n);
            if (registry_.info(ns).flags & kAir) continue;
            const StateId updated = updateShape(registry_, *this, ns, n, opposite(d), qs);
            if (updated != ns && setBlock(n, updated)) {
                pending.push_back(n);
                --budget;
            }
        }
    }
    return true;
}

void Level::attachStorage(std::shared_ptr<const ChunkStorage> storage) {
    chunks_.setStorage(std::move(storage));
    chunks_.setPersistenceHooks(
        [this](ChunkPos c, bool unloading) {
            // The chunk's scheduled ticks, with delays relative to now.
            Json ticks = Json::array();
            std::vector<ScheduledTick> keep;
            for (const ScheduledTick& t : scheduled_) {
                if (ChunkPos::of(t.pos) != c) {
                    keep.push_back(t);
                    continue;
                }
                ticks.push_back({t.pos.x, t.pos.y, t.pos.z, registry_.block(t.block).id, std::max<int64_t>(t.time - gameTime_, 1)});
                if (!unloading) keep.push_back(t);
            }
            if (unloading) {
                scheduled_ = std::move(keep);
                std::make_heap(scheduled_.begin(), scheduled_.end(), std::greater<>());
            }
            return ticks.empty() ? Json() : Json{{"ticks", std::move(ticks)}};
        },
        [this](ChunkPos, const Json& extra) {
            for (const Json& t : extra.value("ticks", Json::array())) {
                if (!t.is_array() || t.size() != 5) continue;
                const Block* b = registry_.find(t[3].get<std::string>());
                if (b) scheduleTick({t[0].get<int>(), t[1].get<int>(), t[2].get<int>()}, b->index, t[4].get<int>());
            }
        });
}

Json Level::saveData() const {
    return {{"game_time", gameTime_}, {"day_time", dayTime_}, {"daylight_cycle", daylightCycle}};
}

void Level::loadData(const Json& j) {
    gameTime_ = j.value("game_time", gameTime_);
    dayTime_ = j.value("day_time", dayTime_);
    daylightCycle = j.value("daylight_cycle", daylightCycle);
}

} // namespace mc
