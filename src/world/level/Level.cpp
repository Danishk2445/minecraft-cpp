#include "world/level/Level.h"

#include <algorithm>
#include <functional>

#include "entity/ExperienceOrb.h"
#include "entity/FallingBlockEntity.h"
#include "entity/ItemEntity.h"
#include "entity/LightningBolt.h"
#include "entity/Player.h"
#include "entity/PrimedTnt.h"
#include "game/BlockEntities.h"
#include "game/Loot.h"
#include "world/block/BlockBehavior.h"
#include "world/block/BlockRules.h"
#include "world/level/Environment.h"
#include "world/sim/Simulation.h"

namespace mc {
namespace {

// Saved fluid tick types (sim::fluidTickType 1..4), vanilla's fluid ids.
constexpr const char* kFluidTickNames[4] = {"minecraft:water", "minecraft:flowing_water", "minecraft:lava",
                                            "minecraft:flowing_lava"};

} // namespace

Level::Level(const BlockRegistry& registry, std::shared_ptr<const ChunkGenerator> generator, JobSystem& jobs,
             int64_t seed)
    : registry_(registry), chunks_(registry, std::move(generator), jobs), seed_(seed),
      random_(seed ^ 0x5DEECE66DLL) {}

void Level::tick() {
    // Vanilla ServerLevel.tick: time, scheduled block then fluid ticks, the chunks'
    // random ticks, entities, block entities.
    advanceWeather();
    ++gameTime_;
    if (daylightCycle) ++dayTime_;
    runScheduledTicks();
    tickChunks();
    tickEntities();
    tickBlockEntities();
}

void Level::tickChunks() {
    for (const ChunkManager::NewlyTicking& n : chunks_.takeNewlyTicking())
        if (n.generated) sim::postProcessChunk(*this, n.pos);
    const bool thunder = isRaining() && isThundering();
    for (const ChunkPos c : chunks_.tickingChunks()) {
        const Chunk* chunk = chunks_.chunk(c);
        if (!chunk) continue;
        // Vanilla tickChunk: lightning strikes 1 in 100000 chunk ticks in a thunderstorm,
        // then precipitation 1 in 48 per random tick.
        if (thunder && random_.nextInt(100000) == 0) {
            const BlockPos at = randomPosIn(c, 0);
            spawnLightning(BlockPos(at.x, motionBlockingHeight(at.x, at.z), at.z), false);
        }
        for (int k = 0; k < randomTickSpeed; ++k)
            if (random_.nextInt(48) == 0) tickPrecipitation(randomPosIn(c, 0));
        if (randomTickSpeed <= 0) continue;
        for (int si = 0; si < kSectionCount; ++si) {
            if (!chunk->sections[size_t(si)].blocks) continue; // all air
            const int y0 = sectionMinY(si);
            for (int k = 0; k < randomTickSpeed; ++k) {
                // Vanilla getBlockRandomPos: an LCG, not the level's random.
                randValue_ = int32_t(uint32_t(randValue_) * 3u + 1013904223u);
                const int r = randValue_ >> 2;
                const BlockPos p(c.x * 16 + (r & 15), y0 + ((r >> 16) & 15), c.z * 16 + ((r >> 8) & 15));
                const StateId state = chunk->get(r & 15, p.y, (r >> 8) & 15);
                if (sim::randomlyTicks(registry_, state)) sim::randomTick(*this, p, state);
            }
        }
    }
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
    else if (type == "minecraft:falling_block") e = std::make_unique<FallingBlockEntity>(registry_);
    else if (type == "minecraft:tnt") e = std::make_unique<PrimedTnt>();
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
    if (auto* falling = dynamic_cast<FallingBlockEntity*>(e.get()); falling && (registry_.info(falling->state).flags & kAir))
        return nullptr;
    return e;
}

void Level::TickQueue::counted(const BlockPos& p, int delta) {
    const ChunkPos c = ChunkPos::of(p);
    if ((perChunk_[c] += delta) <= 0) perChunk_.erase(c);
}

bool Level::TickQueue::schedule(const BlockPos& p, uint16_t type, int64_t time) {
    if (!pending_.insert(Key{p, type}).second) return false;
    heap_.push_back({time, seq_++, p, type});
    std::push_heap(heap_.begin(), heap_.end(), std::greater<>());
    counted(p, 1);
    return true;
}

Level::TickQueue::Tick Level::TickQueue::pop() {
    std::pop_heap(heap_.begin(), heap_.end(), std::greater<>());
    const Tick t = heap_.back();
    heap_.pop_back();
    pending_.erase(Key{t.pos, t.type});
    counted(t.pos, -1);
    return t;
}

std::vector<Level::TickQueue::Tick> Level::TickQueue::inChunk(ChunkPos c, bool remove) {
    std::vector<Tick> out;
    if (!remove) {
        for (const Tick& t : heap_)
            if (ChunkPos::of(t.pos) == c) out.push_back(t);
    } else {
        std::vector<Tick> keep;
        for (const Tick& t : heap_) {
            if (ChunkPos::of(t.pos) == c) {
                out.push_back(t);
                pending_.erase(Key{t.pos, t.type});
                counted(t.pos, -1);
            } else {
                keep.push_back(t);
            }
        }
        heap_ = std::move(keep);
        std::make_heap(heap_.begin(), heap_.end(), std::greater<>());
    }
    std::sort(out.begin(), out.end(), [](const Tick& a, const Tick& b) { return a.time != b.time ? a.time < b.time : a.seq < b.seq; });
    return out;
}

void Level::scheduleTick(const BlockPos& p, uint16_t block, int delay) {
    blockTicks_.schedule(p, block, gameTime_ + std::max(delay, 1));
}

void Level::scheduleFluidTick(const BlockPos& p, uint16_t fluidType, int delay) {
    if (fluidType != 0) fluidTicks_.schedule(p, fluidType, gameTime_ + std::max(delay, 1));
}

bool Level::hasScheduledTick(const BlockPos& p, uint16_t block) const { return blockTicks_.contains(p, block); }

void Level::runScheduledTicks() {
    // Up to vanilla's 65536 of each kind per game tick; due ticks whose chunk is not ticking
    // wait for it.
    for (const bool fluids : {false, true}) {
        TickQueue& queue = fluids ? fluidTicks_ : blockTicks_;
        std::vector<TickQueue::Tick> waiting;
        for (int budget = 65536; budget > 0 && queue.due(gameTime_); --budget) {
            const TickQueue::Tick t = queue.pop();
            if (!chunks_.isReady(ChunkPos::of(t.pos))) {
                waiting.push_back(t);
                continue;
            }
            const StateId s = getBlock(t.pos);
            if (fluids) {
                sim::fluidTick(*this, t.pos, s, t.type);
            } else if (registry_.info(s).block == t.type) { // else the block changed meanwhile
                sim::blockTick(*this, t.pos, s);
            }
        }
        for (const TickQueue::Tick& t : waiting) queue.schedule(t.pos, t.type, gameTime_ + 1);
    }
}

bool Level::destroyBlock(const BlockPos& p, bool drop) {
    const StateId s = getBlock(p);
    if (registry_.info(s).flags & kAir) return false;
    levelEvent(LevelEventType::BlockBroken, p, s);
    if (drop) {
        dropResources(p, s);
        // What a container held spills (vanilla preRemoveSideEffects).
        if (auto it = blockEntities_.find(p); it != blockEntities_.end())
            for (const ItemStack& stack : it->second->contents()) ItemEntity::spill(*this, p, stack);
    }
    // A block that held water leaves it (vanilla fluidState.createLegacyBlock).
    const FluidState& f = registry_.info(s).fluid;
    StateId after = BlockRegistry::kAirState;
    if (registry_.blockOf(s).className != "liquid" && f.kind == FluidKind::Water && f.source())
        after = registry_.defaultState("water");
    return setBlockAndUpdate(p, after);
}

void Level::dropResources(const BlockPos& p, StateId state) {
    if (!doTileDrops || !loot_ || !items_) return;
    for (const ItemStack& drop : loot_->drops(state, false, random_)) ItemEntity::popResource(*this, p, drop);
}

BiomeId Level::biomeAt(const BlockPos& p) const {
    const Chunk* c = chunks_.chunk(ChunkPos::of(p));
    if (!c) return 0;
    const int y = std::clamp(p.y, kWorldMinY, kWorldMaxY);
    const Section& s = c->sections[size_t(sectionIndexOfY(y))];
    return s.biomes[size_t(((((y - kWorldMinY) & 15) >> 2) << 4) | (((p.z & 15) >> 2) << 2) | ((p.x & 15) >> 2))];
}

int Level::skyDarken() const { return skyDarkenLevel(dayTime_, rainLevel(1.0f), thunderLevel(1.0f)); }

float Level::rainLevel(float partialTick) const { return oRain_ + (rain_ - oRain_) * partialTick; }

float Level::thunderLevel(float partialTick) const {
    return (oThunder_ + (thunder_ - oThunder_) * partialTick) * rainLevel(partialTick);
}

void Level::setWeather(int clearTime, int weatherTime, bool rain, bool thunder) {
    clearWeatherTime = clearTime;
    rainTime = weatherTime;
    thunderTime = weatherTime;
    raining = rain;
    thundering = thunder;
}

void Level::advanceWeather() {
    if (doWeatherCycle) {
        if (clearWeatherTime > 0) {
            --clearWeatherTime;
            thunderTime = thundering ? 0 : 1;
            rainTime = raining ? 0 : 1;
            thundering = raining = false;
        } else {
            // Vanilla's UniformInt ranges: thunder lasts 3600-15600 ticks and comes every
            // 12000-180000; rain lasts 12000-24000 and comes every 12000-180000.
            if (thunderTime > 0) {
                if (--thunderTime == 0) thundering = !thundering;
            } else {
                thunderTime = thundering ? random_.nextIntBetween(3600, 15600) : random_.nextIntBetween(12000, 180000);
            }
            if (rainTime > 0) {
                if (--rainTime == 0) raining = !raining;
            } else {
                rainTime = raining ? random_.nextIntBetween(12000, 24000) : random_.nextIntBetween(12000, 180000);
            }
        }
    }
    oThunder_ = thunder_;
    thunder_ = std::clamp(thunder_ + (thundering ? 0.01f : -0.01f), 0.0f, 1.0f);
    oRain_ = rain_;
    rain_ = std::clamp(rain_ + (raining ? 0.01f : -0.01f), 0.0f, 1.0f);
}

Level::Precipitation Level::precipitationAt(const BlockPos& p) const {
    if (!biomes_) return Precipitation::Rain;
    const Biome& b = biomes_->get(biomeAt(p));
    if (!b.precipitation) return Precipitation::None;
    return b.coldEnoughToSnow(p.x, p.y, p.z) ? Precipitation::Snow : Precipitation::Rain;
}

bool Level::isRainingAt(const BlockPos& p) const {
    return isRaining() && motionBlockingHeight(p.x, p.z) <= p.y && precipitationAt(p) == Precipitation::Rain;
}

int Level::motionBlockingHeight(int x, int z) const {
    const Chunk* c = chunks_.chunk(ChunkPos::of(BlockPos(x, 0, z)));
    if (!c) return kWorldMinY;
    for (int si = kSectionCount - 1; si >= 0; --si) {
        const auto& blocks = c->sections[size_t(si)].blocks;
        if (!blocks) continue;
        for (int y = 15; y >= 0; --y) {
            const StateInfo& i = registry_.info(blocks->states[size_t((y << 8) | ((z & 15) << 4) | (x & 15))]);
            if ((i.flags & kBlocksMotion) || !i.fluid.empty()) return sectionMinY(si) + y + 1;
        }
    }
    return kWorldMinY;
}

void Level::spawnLightning(const BlockPos& column, bool visualOnly) {
    BlockPos at(column.x, motionBlockingHeight(column.x, column.z), column.z);
    if (player_ && !player_->dead()) {
        const AABB around(at.x - 3.0, at.y - 3.0, at.z - 3.0, at.x + 4.0, kWorldMaxY + 4.0, at.z + 4.0);
        const BlockPos feet = BlockPos::containing(player_->pos);
        if (player_->box().intersects(around) && motionBlockingHeight(feet.x, feet.z) <= feet.y) at = feet;
    }
    if (at.y == kWorldMinY) at = at.above(2); // a hole to the void
    if (!isRainingAt(at)) return;
    LightningBolt::strike(*this, at, visualOnly);
}

BlockPos Level::randomPosIn(ChunkPos c, int y) {
    // Vanilla getBlockRandomPos: the random tick LCG.
    randValue_ = int32_t(uint32_t(randValue_) * 3u + 1013904223u);
    const int r = randValue_ >> 2;
    return {c.minBlockX() + (r & 15), y + ((r >> 16) & 15), c.minBlockZ() + ((r >> 8) & 15)};
}

void Level::tickPrecipitation(const BlockPos& column) {
    const BlockPos top(column.x, motionBlockingHeight(column.x, column.z), column.z);
    const BlockPos below = top.below();
    if (!biomes_) return;
    const Biome& biome = biomes_->get(biomeAt(top));
    auto blockLight = [&](const BlockPos& p) { return lightAt(p) & 15; };
    // Biome.shouldFreeze: still water at the edge of open water, cold and dark enough.
    if (biome.coldEnoughToSnow(below.x, below.y, below.z) && blockLight(below) < 10) {
        const StateId s = getBlock(below);
        auto isWater = [&](const BlockPos& p) { return registry_.info(getBlock(p)).fluid.kind == FluidKind::Water; };
        if (registry_.blockOf(s).className == "liquid" && registry_.info(s).fluid.kind == FluidKind::Water &&
            registry_.info(s).fluid.source() &&
            !(isWater(below.offset(Direction::West)) && isWater(below.offset(Direction::East)) &&
              isWater(below.offset(Direction::North)) && isWater(below.offset(Direction::South))))
            setBlockAndUpdate(below, registry_.defaultState("ice"));
    }
    if (!isRaining()) return;
    // Biome.shouldSnow: snow piles up layer by layer in the cold.
    if (snowAccumulationHeight > 0 && biome.coldEnoughToSnow(top.x, top.y, top.z) && blockLight(top) < 10) {
        const StateId s = getBlock(top);
        const bool snow = registry_.blockOf(s).className == "snow_layer";
        const StateId layer = registry_.defaultState("snow");
        if (((registry_.info(s).flags & kAir) || snow) && registry_.rules().canSurvive(*this, layer, top)) {
            if (!snow) {
                setBlockAndUpdate(top, layer);
            } else if (const int layers = registry_.intValue(s, "layers"); layers < std::min(snowAccumulationHeight, 8)) {
                setBlockAndUpdate(top, *registry_.with(s, "layers", std::to_string(layers + 1)));
            }
        }
    }
    // Cauldrons catch rain (1 in 20) and snow (1 in 10).
    const Precipitation precipitation = precipitationAt(below);
    if (precipitation == Precipitation::None) return;
    const StateId s = getBlock(below);
    const std::string_view id = registry_.blockOf(s).id;
    const bool rain = precipitation == Precipitation::Rain;
    if (id == "minecraft:cauldron") {
        if (random_.nextFloat() < (rain ? 0.05f : 0.1f))
            setBlockAndUpdate(below, registry_.defaultState(rain ? "water_cauldron" : "powder_snow_cauldron"));
    } else if (id == (rain ? "minecraft:water_cauldron" : "minecraft:powder_snow_cauldron")) {
        if (random_.nextFloat() < (rain ? 0.05f : 0.1f))
            if (const int l = registry_.intValue(s, "level"); l < 3)
                setBlockAndUpdate(below, *registry_.with(s, "level", std::to_string(l + 1)));
    }
}

int Level::rawBrightness(const BlockPos& p, int darken) const {
    const uint8_t l = lightAt(p);
    return std::max(int(l >> 4) - darken, int(l & 15));
}

uint8_t Level::lightAt(const BlockPos& p) const {
    const Chunk* c = chunks_.chunk(ChunkPos::of(p));
    return c ? c->lightAt(p.x & 15, p.y, p.z & 15) : packLight(15, 0);
}

bool Level::setBlockAndUpdate(const BlockPos& p, StateId state) {
    const StateId old = getBlock(p);
    if (!setBlock(p, state)) return false;
    if (old != state) updateAround(p, old, state);
    return true;
}

void Level::updateAround(const BlockPos& p, StateId old, StateId state) {
    // Vanilla markAndNotifyBlock: the block's onPlace, its neighbors notified (in vanilla's
    // order: west, east, down, up, north, south), then the shapes around it.
    sim::onPlace(*this, p, state, old);
    for (Direction d : {Direction::West, Direction::East, Direction::Down, Direction::Up, Direction::North,
                        Direction::South}) {
        const BlockPos n = p.offset(d);
        sim::neighborChanged(*this, n, getBlock(n), p);
    }
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
            sim::shapeUpdated(*this, n, ns, opposite(d), qs);
            if (getBlock(n) != ns) continue; // the simulation replaced it already
            if (updated != ns && setBlock(n, updated)) {
                pending.push_back(n);
                --budget;
                // Removed (to air, or to the water it held): it breaks like vanilla's destroyBlock,
                // with its drops, particles and its own neighbors told.
                const bool gone = (registry_.info(updated).flags & kAir) ||
                                  (registry_.info(updated).fluid.kind != FluidKind::None &&
                                   registry_.blockOf(updated).index != registry_.blockOf(ns).index &&
                                   registry_.info(ns).fluid.kind == registry_.info(updated).fluid.kind);
                sim::onPlace(*this, n, updated, ns);
                if (gone) {
                    levelEvent(LevelEventType::BlockBroken, n, ns);
                    if (loot_ && items_ && loot_->dropsWhenUpdated(ns)) dropResources(n, ns);
                    for (Direction e : kDirections)
                        if (n.offset(e) != q) sim::neighborChanged(*this, n.offset(e), getBlock(n.offset(e)), n);
                }
            }
        }
    }
}

void Level::attachStorage(std::shared_ptr<const ChunkStorage> storage) {
    chunks_.setStorage(std::move(storage));
    chunks_.setPersistenceHooks(
        [this](ChunkPos c, bool unloading) {
            // The chunk's scheduled ticks, with delays relative to now.
            Json ticks = Json::array(), fluidTicks = Json::array();
            for (const TickQueue::Tick& t : blockTicks_.inChunk(c, unloading))
                ticks.push_back({t.pos.x, t.pos.y, t.pos.z, registry_.block(t.type).id, std::max<int64_t>(t.time - gameTime_, 1)});
            for (const TickQueue::Tick& t : fluidTicks_.inChunk(c, unloading))
                fluidTicks.push_back({t.pos.x, t.pos.y, t.pos.z, kFluidTickNames[std::clamp<int>(t.type, 1, 4) - 1],
                                      std::max<int64_t>(t.time - gameTime_, 1)});
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
            if (!fluidTicks.empty()) extra["fluid_ticks"] = std::move(fluidTicks);
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
            for (const Json& t : extra.value("fluid_ticks", Json::array())) {
                if (!t.is_array() || t.size() != 5 || !t[3].is_string()) continue;
                for (uint16_t type = 1; type <= 4; ++type)
                    if (t[3].get<std::string>() == kFluidTickNames[type - 1])
                        scheduleFluidTick({t[0].get<int>(), t[1].get<int>(), t[2].get<int>()}, type, t[4].get<int>());
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
        // A chunk is saved for its extras too: entities (when it has some, or its file lists
        // some), block entities and pending ticks.
        [this](ChunkPos c) {
            if (savedEntityChunks_.contains(c) || blockTicks_.anyInChunk(c) || fluidTicks_.anyInChunk(c)) return true;
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
            {"natural_regeneration", naturalRegeneration},
            {"random_tick_speed", randomTickSpeed},
            {"do_fire_tick", doFireTick},
            {"water_source_conversion", waterSourceConversion},
            {"lava_source_conversion", lavaSourceConversion},
            {"do_tile_drops", doTileDrops},
            {"do_entity_drops", doEntityDrops},
            {"do_vines_spread", doVinesSpread},
            {"do_weather_cycle", doWeatherCycle},
            {"snow_accumulation_height", snowAccumulationHeight},
            {"clear_weather_time", clearWeatherTime},
            {"rain_time", rainTime},
            {"thunder_time", thunderTime},
            {"raining", raining},
            {"thundering", thundering}};
}

void Level::loadData(const Json& j) {
    gameTime_ = j.value("game_time", gameTime_);
    dayTime_ = j.value("day_time", dayTime_);
    daylightCycle = j.value("daylight_cycle", daylightCycle);
    keepInventory = j.value("keep_inventory", keepInventory);
    naturalRegeneration = j.value("natural_regeneration", naturalRegeneration);
    randomTickSpeed = std::max(0, j.value("random_tick_speed", randomTickSpeed));
    doFireTick = j.value("do_fire_tick", doFireTick);
    waterSourceConversion = j.value("water_source_conversion", waterSourceConversion);
    lavaSourceConversion = j.value("lava_source_conversion", lavaSourceConversion);
    doTileDrops = j.value("do_tile_drops", doTileDrops);
    doEntityDrops = j.value("do_entity_drops", doEntityDrops);
    doVinesSpread = j.value("do_vines_spread", doVinesSpread);
    doWeatherCycle = j.value("do_weather_cycle", doWeatherCycle);
    snowAccumulationHeight = std::clamp(j.value("snow_accumulation_height", snowAccumulationHeight), 0, 8);
    clearWeatherTime = j.value("clear_weather_time", clearWeatherTime);
    rainTime = j.value("rain_time", rainTime);
    thunderTime = j.value("thunder_time", thunderTime);
    raining = j.value("raining", raining);
    thundering = j.value("thundering", thundering);
    settleWeather(); // vanilla prepareWeather: a world saved in the rain loads raining
}

} // namespace mc
