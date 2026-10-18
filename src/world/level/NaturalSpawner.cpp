#include "world/level/NaturalSpawner.h"

#include <cmath>
#include <unordered_set>

#include "entity/Player.h"
#include "entity/mob/Mob.h"
#include "entity/mob/Monsters.h"
#include "entity/path/PathTypes.h"
#include "entity/Physics.h"
#include "resources/ResourcePack.h"
#include "world/level/Level.h"

namespace mc::spawner {
namespace {

constexpr double kDespawnDistanceSq = 128.0 * 128.0;
constexpr int kMagicNumber = 289; // 17 x 17 chunks

// The chunks spawning happens in: ticking, their middle within 128 blocks of a player who
// isn't spectating (vanilla playerIsCloseEnoughForSpawning).
std::vector<ChunkPos> spawnableChunks(const Level& level) {
    std::vector<ChunkPos> out;
    const Player* p = level.player();
    if (!p || p->mode() == GameMode::Spectator) return out;
    for (const ChunkPos c : level.chunks().tickingChunks()) {
        const double dx = c.x * 16 + 8 - p->pos.x, dz = c.z * 16 + 8 - p->pos.z;
        if (dx * dx + dz * dz < 16384.0) out.push_back(c);
    }
    return out;
}

// One above the highest block that isn't air (vanilla WORLD_SURFACE).
int worldSurface(const Level& level, int x, int z) {
    const Chunk* c = level.chunks().chunk(ChunkPos::of(BlockPos(x, 0, z)));
    if (!c) return kWorldMinY;
    const BlockRegistry& reg = level.registry();
    for (int si = kSectionCount - 1; si >= 0; --si) {
        if (!c->sections[size_t(si)].blocks) continue;
        const int y0 = sectionMinY(si);
        for (int y = y0 + 15; y >= y0; --y)
            if (!(reg.info(c->get(x & 15, y, z & 15)).flags & kAir)) return y + 1;
    }
    return kWorldMinY;
}

bool isSignalSource(const Block& b) {
    static const std::unordered_set<std::string_view> kClasses = {
        "lever", "button", "pressure_plate", "weighted_pressure_plate", "redstone_torch", "redstone_wall_torch",
        "redstone_wire", "repeater", "comparator", "observer", "lightning_rod"};
    const std::string_view id = shortId(b.id);
    return kClasses.contains(b.className) || id == "redstone_block" || id == "daylight_detector" || id == "target" ||
           id == "trapped_chest" || id == "tripwire_hook" || id == "sculk_sensor" || id == "calibrated_sculk_sensor" ||
           id == "detector_rail";
}

bool isDangerous(const BlockRegistry& reg, StateId s) {
    const std::string_view id = shortId(reg.blockOf(s).id);
    return pathBlockInfo(reg, s).burning || id == "wither_rose" || id == "sweet_berry_bush" || id == "cactus" ||
           id == "powder_snow";
}

bool isValidEmptySpawnBlock(const Level& level, const BlockPos& p) {
    const BlockRegistry& reg = level.registry();
    const StateId s = level.getBlock(p);
    const Shape& shape = reg.collisionShape(s);
    if (shape.size() == 1 && shape[0].min == glm::dvec3(0.0) && shape[0].max == glm::dvec3(1.0)) return false;
    if (isSignalSource(reg.blockOf(s)) || !reg.info(s).fluid.empty()) return false;
    static thread_local const BlockRegistry* cached = nullptr;
    static thread_local TagId rails = 0;
    if (cached != &reg) {
        cached = &reg;
        rails = reg.tag("rails").value_or(TagId(0));
    }
    if (reg.hasTag(s, rails)) return false;
    return !isDangerous(reg, s);
}

AABB spawnBox(const EntityTypeInfo& type, double x, double y, double z) {
    const double hw = type.width / 2.0;
    return AABB(x - hw, y, z - hw, x + hw, y + type.height, z + hw);
}

bool anyLiquid(const Level& level, const AABB& b) {
    for (int y = int(std::floor(b.min.y)); y <= int(std::floor(b.max.y - 1e-7)); ++y)
        for (int z = int(std::floor(b.min.z)); z <= int(std::floor(b.max.z - 1e-7)); ++z)
            for (int x = int(std::floor(b.min.x)); x <= int(std::floor(b.max.x - 1e-7)); ++x)
                if (!level.fluid({x, y, z}).empty()) return true;
    return false;
}

const SpawnerData* pickWeighted(const std::vector<SpawnerData>& list, Xoroshiro& random) {
    int total = 0;
    for (const SpawnerData& d : list) total += d.weight;
    if (total <= 0) return nullptr;
    int roll = random.nextInt(total);
    for (const SpawnerData& d : list) {
        roll -= d.weight;
        if (roll < 0) return &d;
    }
    return nullptr;
}

std::unique_ptr<Mob> makeMob(Level& level, const EntityTypeInfo& type) {
    const EntityTypes* types = level.entityTypes();
    if (!types) return nullptr;
    std::unique_ptr<Mob> mob = createMob(*types, type.id);
    if (mob) mob->random = Xoroshiro(int64_t(level.random().nextLong()));
    return mob;
}

// Vanilla spawnCategoryForPosition: three packs from one spot, each a few steps apart.
void spawnCategoryForPosition(Level& level, MobCategory category, ChunkPos chunk, const BlockPos& start, int& spawned,
                              int cap) {
    const BlockRegistry& reg = level.registry();
    if (reg.info(level.getBlock(start)).flags & kOpaqueCube) return; // vanilla isRedstoneConductor
    Xoroshiro& r = level.random();
    const Player* player = level.player();
    const glm::dvec3 worldSpawn = glm::dvec3(level.generator().spawnPoint()) + glm::dvec3(0.5, 0.0, 0.5);
    int inPos = 0;
    for (int pack = 0; pack < 3; ++pack) {
        int x = start.x, z = start.z;
        const SpawnerData* data = nullptr;
        SpawnGroup group;
        int size = int(std::ceil(r.nextFloat() * 4.0f));
        for (int i = 0; i < size; ++i) {
            x += r.nextInt(6) - r.nextInt(6);
            z += r.nextInt(6) - r.nextInt(6);
            const BlockPos p(x, start.y, z);
            const double cx = x + 0.5, cz = z + 0.5;
            if (!player) continue;
            const glm::dvec3 d = player->pos - glm::dvec3(cx, start.y, cz);
            const double distanceSq = glm::dot(d, d);
            // Not within 24 blocks of the player or the world spawn, in a chunk that ticks.
            if (distanceSq <= 576.0) continue;
            const glm::dvec3 ws = glm::dvec3(cx, start.y, cz) - worldSpawn;
            if (glm::dot(ws, ws) < 576.0) continue;
            if (ChunkPos::of(p) != chunk && !level.chunks().isReady(ChunkPos::of(p))) continue;
            if (!data) {
                const EntityTypes* types = level.entityTypes();
                if (!types) return;
                data = pickWeighted(types->spawns(level.biomeAt(p), category), r);
                if (!data) break;
                size = data->minCount + r.nextInt(1 + data->maxCount - data->minCount);
            }
            const EntityTypeInfo& type = *data->type;
            // Monsters never spawn beyond the despawn distance.
            if (type.category == MobCategory::Monster && distanceSq > kDespawnDistanceSq) continue;
            if (!isSpawnPositionOk(level, p) || !checkSpawnRules(level, type, p, r)) continue;
            if (!noCollision(level, spawnBox(type, cx, start.y, cz))) continue;
            std::unique_ptr<Mob> mob = makeMob(level, type);
            if (!mob) return;
            mob->pos = mob->oldPos = glm::dvec3(cx, start.y, cz);
            mob->setRotation(r.nextFloat() * 360.0f, 0.0f);
            if (distanceSq > kDespawnDistanceSq && mob->removeWhenFarAway(distanceSq)) continue;
            if (!mob->checkSpawnRules(level, SpawnReason::Natural) || anyLiquid(level, mob->box())) continue;
            mob->finalizeSpawn(level, SpawnReason::Natural, group);
            const int maxCluster = mob->maxSpawnClusterSize();
            level.addEntity(std::move(mob));
            ++spawned;
            ++inPos;
            if (inPos >= maxCluster || spawned >= cap) return;
        }
    }
}

} // namespace

bool isValidSpawnFloor(const Level& level, const BlockPos& p) {
    const BlockRegistry& reg = level.registry();
    const StateId s = level.getBlock(p);
    if (!level.sturdy(p, Direction::Up) || reg.info(s).emission >= 14) return false;
    const std::string_view id = shortId(reg.blockOf(s).id);
    if (id.find("glass") != std::string_view::npos || reg.blockOf(s).className == "leaves") return false;
    return id != "ice" && id != "frosted_ice" && id != "barrier" && id != "bedrock" && id != "magma_block" &&
           id != "light";
}

bool isSpawnPositionOk(const Level& level, const BlockPos& p) {
    if (p.y <= kWorldMinY || p.y >= kWorldMaxY) return false;
    return isValidSpawnFloor(level, p.offset(0, -1, 0)) && isValidEmptySpawnBlock(level, p) &&
           isValidEmptySpawnBlock(level, p.offset(0, 1, 0));
}

bool checkSpawnRules(const Level& level, const EntityTypeInfo& type, const BlockPos& p, Xoroshiro& random) {
    if (type.category == MobCategory::Creature) {
        // Vanilla Animal.checkAnimalSpawnRules.
        static thread_local const BlockRegistry* cached = nullptr;
        static thread_local TagId spawnable = 0;
        if (cached != &level.registry()) {
            cached = &level.registry();
            spawnable = level.registry().tag("animals_spawnable_on").value_or(TagId(0));
        }
        return level.hasTag(p.offset(0, -1, 0), spawnable) && level.rawBrightness(p, 0) > 8;
    }
    if (type.category == MobCategory::Monster) {
        // Vanilla Monster.checkMonsterSpawnRules.
        return level.difficulty != Difficulty::Peaceful && isDarkEnoughToSpawn(level, p, random) &&
               isValidSpawnFloor(level, p.offset(0, -1, 0));
    }
    return false;
}

int mobCount(const Level& level, MobCategory category) {
    int n = 0;
    for (const auto& e : level.entities()) {
        const auto* mob = dynamic_cast<const Mob*>(e.get());
        if (mob && !mob->removed && !mob->persistenceRequired && mob->category() == category) ++n;
    }
    return n;
}

int localCap(const Level& level, MobCategory category) {
    return mobCap(category) * int(spawnableChunks(level).size()) / kMagicNumber;
}

void tick(Level& level) {
    if (!level.doMobSpawning || !level.entityTypes() || !level.player()) return;
    const bool enemies = level.difficulty != Difficulty::Peaceful;
    const bool animals = level.gameTime() % 400 == 0;
    if (!enemies && !animals) return;
    std::vector<ChunkPos> chunks = spawnableChunks(level);
    if (chunks.empty()) return;
    Xoroshiro& r = level.random();
    for (size_t i = chunks.size(); i > 1; --i) std::swap(chunks[i - 1], chunks[size_t(r.nextInt(int(i)))]);
    const int count = int(chunks.size());
    struct Category {
        MobCategory category;
        bool active;
        int cap, current;
    };
    Category categories[2] = {
        {MobCategory::Monster, enemies, mobCap(MobCategory::Monster) * count / kMagicNumber, mobCount(level, MobCategory::Monster)},
        {MobCategory::Creature, animals, mobCap(MobCategory::Creature) * count / kMagicNumber,
         mobCount(level, MobCategory::Creature)},
    };
    for (const ChunkPos c : chunks)
        for (Category& cat : categories) {
            if (!cat.active || cat.current >= cat.cap) continue;
            // Vanilla getRandomPosWithin: any height from the bottom to just above the surface.
            const int x = c.x * 16 + r.nextInt(16), z = c.z * 16 + r.nextInt(16);
            const int top = worldSurface(level, x, z) + 1;
            const int y = kWorldMinY + r.nextInt(top - kWorldMinY + 1);
            if (y < kWorldMinY + 1) continue;
            spawnCategoryForPosition(level, cat.category, c, {x, y, z}, cat.current, cat.cap);
        }
}

int populateChunk(Level& level, ChunkPos chunk) {
    const EntityTypes* types = level.entityTypes();
    if (!types || !level.doMobSpawning) return 0;
    const int x0 = chunk.x * 16, z0 = chunk.z * 16;
    const BiomeId biome = level.biomeAt({x0, kWorldMaxY - 1, z0});
    const std::vector<SpawnerData>& list = types->spawns(biome, MobCategory::Creature);
    if (list.empty()) return 0;
    // A per-chunk random (vanilla's decoration seed): the same chunk always rolls the same.
    Xoroshiro r(int64_t(mixStafford13(uint64_t(level.seed()) ^ (uint64_t(uint32_t(chunk.x)) << 32 | uint32_t(chunk.z)))));
    const BlockRegistry& reg = level.registry();
    int spawned = 0;
    while (r.nextFloat() < types->creatureProbability(biome)) {
        const SpawnerData* data = pickWeighted(list, r);
        if (!data) break;
        const int size = data->minCount + r.nextInt(1 + data->maxCount - data->minCount);
        SpawnGroup group;
        int x = x0 + r.nextInt(16), z = z0 + r.nextInt(16);
        const int ox = x, oz = z;
        for (int m = 0; m < size; ++m) {
            bool done = false;
            for (int tries = 0; !done && tries < 4; ++tries) {
                // Vanilla getTopNonCollidingPos: on the surface, or in the top block when one can stand in it.
                BlockPos p(x, level.motionBlockingHeight(x, z), z);
                if (pathBlockInfo(reg, level.getBlock(p.offset(0, -1, 0))).pathfindable &&
                    !(reg.info(level.getBlock(p.offset(0, -1, 0))).flags & kAir))
                    p = p.offset(0, -1, 0);
                if (isSpawnPositionOk(level, p)) {
                    const float w = data->type->width;
                    const double cx = std::clamp(double(x), double(x0) + w, double(x0) + 16.0 - w);
                    const double cz = std::clamp(double(z), double(z0) + w, double(z0) + 16.0 - w);
                    if (noCollision(level, spawnBox(*data->type, cx, p.y, cz)) &&
                        checkSpawnRules(level, *data->type, BlockPos::containing({cx, double(p.y), cz}), r)) {
                        std::unique_ptr<Mob> mob = makeMob(level, *data->type);
                        if (mob) {
                            mob->pos = mob->oldPos = glm::dvec3(cx, p.y, cz);
                            mob->setRotation(r.nextFloat() * 360.0f, 0.0f);
                            if (mob->checkSpawnRules(level, SpawnReason::ChunkGeneration) && !anyLiquid(level, mob->box())) {
                                mob->finalizeSpawn(level, SpawnReason::ChunkGeneration, group);
                                level.addEntity(std::move(mob));
                                ++spawned;
                                done = true;
                            }
                        }
                    }
                }
                x += r.nextInt(5) - r.nextInt(5);
                z += r.nextInt(5) - r.nextInt(5);
                while (x < x0 || x >= x0 + 16 || z < z0 || z >= z0 + 16) {
                    x = ox + r.nextInt(5) - r.nextInt(5);
                    z = oz + r.nextInt(5) - r.nextInt(5);
                }
            }
        }
    }
    return spawned;
}

} // namespace mc::spawner
