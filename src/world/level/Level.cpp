#include "world/level/Level.h"

#include <algorithm>
#include <functional>

#include "entity/ExperienceOrb.h"
#include "entity/ItemEntity.h"
#include "entity/Player.h"
#include "game/BlockEntities.h"
#include "game/Loot.h"
#include "world/block/BlockBehavior.h"

namespace mc {

Level::Level(const BlockRegistry& registry, std::shared_ptr<const ChunkGenerator> generator, JobSystem& jobs,
             int64_t seed)
    : registry_(registry), chunks_(registry, std::move(generator), jobs), seed_(seed),
      random_(seed ^ 0x5DEECE66DLL) {}

void Level::tick() {
    ++gameTime_;
    if (daylightCycle) ++dayTime_;
    runScheduledTicks();
    tickBlockEntities();
    tickEntities();
}

bool Level::setBlock(const BlockPos& p, StateId state) {
    const uint16_t before = registry_.info(getBlock(p)).block;
    if (!chunks_.setBlock(p, state)) return false;
    if (registry_.info(state).block != before)
        if (auto it = blockEntities_.find(p); it != blockEntities_.end()) {
            it->second->removed = true;
            blockEntities_.erase(it);
        }
    return true;
}

std::shared_ptr<BlockEntity> Level::blockEntity(const BlockPos& p) {
    if (auto it = blockEntities_.find(p); it != blockEntities_.end()) return it->second;
    if (!chunks_.isReady(ChunkPos::of(p))) return nullptr;
    auto created = createBlockEntity(registry_.blockOf(getBlock(p)).id, p, recipes_);
    if (created) blockEntities_.emplace(p, created);
    return created;
}

void Level::tickBlockEntities() {
    // Copies: a tick may change the block (a furnace lighting up keeps its entity).
    std::vector<std::shared_ptr<BlockEntity>> ticking;
    for (const auto& [pos, be] : blockEntities_)
        if (be->ticks() && chunks_.isReady(ChunkPos::of(pos))) ticking.push_back(be);
    for (const auto& be : ticking) be->tick(*this);
}

Entity& Level::addEntity(std::unique_ptr<Entity> entity) {
    entity->id = nextEntityId_++;
    entities_.push_back(std::move(entity));
    return *entities_.back();
}

std::vector<Entity*> Level::entitiesIn(const AABB& box, const Entity* except) const {
    std::vector<Entity*> out;
    for (const auto& e : entities_)
        if (!e->removed && e.get() != except && e->box().intersects(box)) out.push_back(e.get());
    return out;
}

void Level::recordPickup(const Entity& e, int count) {
    if (auto copy = e.clone()) pickups_.push_back({std::move(copy), count});
}

void Level::tickEntities() {
    // By index: ticks may add entities (merges never do, but drops will).
    for (size_t i = 0; i < entities_.size(); ++i) {
        Entity& e = *entities_[i];
        if (e.removed || !chunks_.isReady(ChunkPos::of(BlockPos::containing(e.pos)))) continue;
        e.tick(*this);
    }
    touchPlayer();
    std::erase_if(entities_, [](const std::unique_ptr<Entity>& e) { return e->removed; });
}

void Level::touchPlayer() {
    if (!player_ || !player_->alive() || player_->mode() == GameMode::Spectator) return;
    for (Entity* e : entitiesIn(player_->box().inflated(1.0, 0.5, 1.0), player_)) {
        if (auto* item = dynamic_cast<ItemEntity*>(e)) item->playerTouch(*this, *player_);
        else if (auto* orb = dynamic_cast<ExperienceOrb*>(e)) orb->playerTouch(*this, *player_);
    }
}

Json Level::saveEntity(const Entity& e) {
    Json j = {{"type", e.type()},
              {"pos", {e.pos.x, e.pos.y, e.pos.z}},
              {"motion", {e.velocity.x, e.velocity.y, e.velocity.z}},
              {"rotation", {e.yaw, e.pitch}}};
    e.saveExtra(j);
    return j;
}

std::unique_ptr<Entity> Level::loadEntity(const Json& j) {
    const std::string type = j.value("type", std::string());
    std::unique_ptr<Entity> e;
    if (type == "minecraft:item") e = std::make_unique<ItemEntity>();
    else if (type == "minecraft:experience_orb") e = std::make_unique<ExperienceOrb>();
    if (!e || !items_) return nullptr;
    auto vec = [&](const char* key) {
        const Json v = j.value(key, Json::array());
        return v.is_array() && v.size() == 3 ? glm::dvec3(v[0].get<double>(), v[1].get<double>(), v[2].get<double>())
                                             : glm::dvec3(0.0);
    };
    e->pos = e->oldPos = vec("pos");
    e->velocity = vec("motion");
    if (const Json r = j.value("rotation", Json::array()); r.is_array() && r.size() == 2) {
        e->yaw = e->oldYaw = r[0].get<float>();
        e->pitch = e->oldPitch = r[1].get<float>();
    }
    e->loadExtra(j, *items_);
    if (auto* item = dynamic_cast<ItemEntity*>(e.get())) {
        if (item->stack.empty()) return nullptr;
        item->bobOffset = random_.nextFloat() * 6.2831855f;
    }
    return e;
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
                // Removed (to air, or to the water it held): it drops like a broken block.
                const bool gone = (registry_.info(updated).flags & kAir) ||
                                  (registry_.info(updated).fluid.kind != FluidKind::None &&
                                   registry_.blockOf(updated).index != registry_.blockOf(ns).index &&
                                   registry_.info(ns).fluid.kind == registry_.info(updated).fluid.kind);
                if (gone && loot_ && items_ && loot_->dropsWhenUpdated(ns))
                    for (const ItemStack& drop : loot_->drops(ns, false, random_)) ItemEntity::popResource(*this, n, drop);
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
            // Its block entities and entities; unloading takes them out of the level.
            Json blockEntities = Json::array();
            for (auto it = blockEntities_.begin(); it != blockEntities_.end();) {
                if (ChunkPos::of(it->first) != c) {
                    ++it;
                    continue;
                }
                Json j = {{"type", it->second->type()}, {"pos", {it->first.x, it->first.y, it->first.z}}};
                it->second->save(j);
                blockEntities.push_back(std::move(j));
                if (unloading) it->second->removed = true;
                it = unloading ? blockEntities_.erase(it) : std::next(it);
            }
            Json entities = Json::array();
            for (auto& e : entities_) {
                if (e->removed || ChunkPos::of(BlockPos::containing(e->pos)) != c) continue;
                entities.push_back(saveEntity(*e));
                if (unloading) e->removed = true;
            }
            if (unloading) std::erase_if(entities_, [](const std::unique_ptr<Entity>& e) { return e->removed; });
            if (entities.empty() && blockEntities.empty()) savedEntityChunks_.erase(c);
            else savedEntityChunks_.insert(c);
            Json extra;
            if (!ticks.empty()) extra["ticks"] = std::move(ticks);
            if (!entities.empty()) extra["entities"] = std::move(entities);
            if (!blockEntities.empty()) extra["block_entities"] = std::move(blockEntities);
            return extra;
        },
        [this](ChunkPos c, const Json& extra) {
            const Json ticks = extra.value("ticks", Json::array()), entities = extra.value("entities", Json::array());
            for (const Json& t : ticks) {
                if (!t.is_array() || t.size() != 5) continue;
                const Block* b = registry_.find(t[3].get<std::string>());
                if (b) scheduleTick({t[0].get<int>(), t[1].get<int>(), t[2].get<int>()}, b->index, t[4].get<int>());
            }
            for (const Json& j : entities)
                if (auto e = loadEntity(j)) addEntity(std::move(e));
            const Json blockEntities = extra.value("block_entities", Json::array());
            for (const Json& j : blockEntities) {
                const Json pos = j.value("pos", Json::array());
                if (!pos.is_array() || pos.size() != 3 || !items_) continue;
                const BlockPos p{pos[0].get<int>(), pos[1].get<int>(), pos[2].get<int>()};
                auto be = createBlockEntity(registry_.blockOf(getBlock(p)).id, p, recipes_);
                if (!be || be->type() != j.value("type", std::string())) continue; // the block changed
                be->load(j, *items_);
                blockEntities_[p] = std::move(be);
            }
            if (!entities.empty() || !blockEntities.empty()) savedEntityChunks_.insert(c);
        },
        // A chunk is saved for its entities too: when it has some, or its file lists some.
        [this](ChunkPos c) {
            if (savedEntityChunks_.contains(c)) return true;
            for (const auto& e : entities_)
                if (!e->removed && ChunkPos::of(BlockPos::containing(e->pos)) == c) return true;
            for (const auto& [pos, be] : blockEntities_)
                if (ChunkPos::of(pos) == c) return true;
            return false;
        });
}

Json Level::saveData() const {
    return {{"game_time", gameTime_},
            {"day_time", dayTime_},
            {"daylight_cycle", daylightCycle},
            {"keep_inventory", keepInventory},
            {"natural_regeneration", naturalRegeneration}};
}

void Level::loadData(const Json& j) {
    gameTime_ = j.value("game_time", gameTime_);
    dayTime_ = j.value("day_time", dayTime_);
    daylightCycle = j.value("daylight_cycle", daylightCycle);
    keepInventory = j.value("keep_inventory", keepInventory);
    naturalRegeneration = j.value("natural_regeneration", naturalRegeneration);
}

} // namespace mc
