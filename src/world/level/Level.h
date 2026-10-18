// Level.h -- one dimension's world state: chunks, time, and block access.
#pragma once

#include "core/Json.h"
#include "world/biome/Biome.h"
#include "world/block/BlockGetter.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/Rng.h"
#include "entity/Entity.h"
#include "game/WorldParams.h"
#include "world/chunk/ChunkManager.h"

namespace mc {

class BlockEntity;
class BlockLoot;
class ChunkStorage;
class BiomeRegistry;
class BiomeDecorator;
class EntityTypes;
class ItemRegistry;
class Player;
class RecipeBook;

// Things that happen in the level for the client to show (vanilla levelEvent): particles
// for now, sounds one day.
enum class LevelEventType : uint8_t {
    BlockBroken, // `state` broke at `pos` (its break particles)
    LavaFizz,    // lava met water or burned something (smoke)
    BoneMeal,    // bone meal used at `pos` (happy-villager sparkles, `data` of them)
    Explosion,   // at `point`, of power `data` (its particles)
    // Entities (M12); `data` packs the entity's box (width | height << 16, in hundredths)
    // around `point` (its feet) where particles fill it.
    EntityPoof,      // a dead mob vanishing (vanilla entity event 60)
    Hearts,          // in love (one heart) or bred (`state` 7 hearts)
    Crit,            // a critical hit on it (vanilla crit particles)
    DamageIndicator, // `state` dark hearts out of what the player's hit took
    Sweep,           // a sword's sweep at `point`
};
struct LevelEvent {
    LevelEventType type;
    BlockPos pos;
    StateId state = 0;
    int data = 0;
    glm::dvec3 point{0.0};
};

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
    // False when the target chunk is not loaded and ready. A different block there drops
    // the old block's block entity (its contents are the caller's business).
    bool setBlock(const BlockPos& p, StateId state);
    // setBlock as vanilla's setBlock with flags 3: the new block's onPlace, the six
    // neighbors' neighborChanged, then shape updates (connections, halves, support)
    // cascading up to vanilla's 512.
    bool setBlockAndUpdate(const BlockPos& p, StateId state);
    // The updates of setBlockAndUpdate for a block already set at `p` (was `old`).
    void updateAround(const BlockPos& p, StateId old, StateId state);
    // Vanilla destroyBlock: the block's drops (when `drop`, and the doTileDrops rule) and
    // break particles, leaving its water if it was waterlogged.
    bool destroyBlock(const BlockPos& p, bool drop);
    // Vanilla dropResources without a player: the block's loot pops out at `p`.
    void dropResources(const BlockPos& p, StateId state);

    void tick();

    // Saves edited chunks (with their scheduled ticks) through `storage`, and loads
    // saved chunks in place of generated ones.
    void attachStorage(std::shared_ptr<const ChunkStorage> storage);
    // Level-wide state for level.json: times and the daylight cycle.
    Json saveData() const;
    void loadData(const Json& j);

    // Vanilla scheduled ticks: `block` at `p` gets ticked after `delay` game ticks (if it
    // is still there then); fluid ticks likewise by fluid type (sim::fluidTickType). One
    // pending tick per position and type: scheduling again while one waits does nothing.
    void scheduleTick(const BlockPos& p, uint16_t block, int delay);
    void scheduleFluidTick(const BlockPos& p, uint16_t fluidType, int delay);
    bool hasScheduledTick(const BlockPos& p, uint16_t block) const;
    size_t pendingTicks() const { return blockTicks_.size(); }
    size_t pendingFluidTicks() const { return fluidTicks_.size(); }

    // What happened for the client to show since the last call.
    void levelEvent(LevelEventType type, const BlockPos& p, StateId state = 0, int data = 0,
                    const glm::dvec3& point = glm::dvec3(0.0)) {
        events_.push_back({type, p, state, data, point});
    }
    std::vector<LevelEvent> takeEvents() { return std::exchange(events_, {}); }

    // Entities (dropped items, experience orbs; later mobs): owned here, ticked while
    // their chunk is ticking, saved and loaded with their chunks.
    Entity& addEntity(std::unique_ptr<Entity> entity);
    const std::vector<std::unique_ptr<Entity>>& entities() const { return entities_; }
    // Live entities whose boxes intersect `box`, except `except`.
    std::vector<Entity*> entitiesIn(const AABB& box, const Entity* except = nullptr) const;
    // The entity (or the player) with this id, if it is still in the level.
    Entity* entity(uint32_t id) const;
    // The player (not owned): orbs drift to it and it picks up what it touches. It gets an
    // id so mobs can target it.
    void setPlayer(Player* player);
    Player* player() const { return player_; }
    // Mob types, spawns and loot (data/entities.json); without them no mob loads or spawns.
    void setEntityTypes(const EntityTypes* types) { entityTypes_ = types; }
    const EntityTypes* entityTypes() const { return entityTypes_; }
    // The item registry, for loading saved entities.
    void setItems(const ItemRegistry* items) { items_ = items; }
    // Block loot: blocks a neighbor update removes (a torch losing its wall, a door's other
    // half) drop theirs, as vanilla's destroyBlock does.
    void setLoot(const BlockLoot* loot) { loot_ = loot; }
    const ItemRegistry* itemRegistry() const { return items_; }
    const BlockLoot* loot() const { return loot_; }
    // Recipes, for furnaces.
    void setRecipes(const RecipeBook* recipes) { recipes_ = recipes; }

    // Block entities (vanilla BlockEntity): chests, barrels, furnaces. The one at `p`,
    // created for its block on first use; null for blocks without one.
    std::shared_ptr<BlockEntity> blockEntity(const BlockPos& p);
    const std::unordered_map<BlockPos, std::shared_ptr<BlockEntity>>& blockEntities() const { return blockEntities_; }
    Xoroshiro& random() { return random_; }
    // What the player picked up this tick (copies as they were), for the pickup animation.
    struct Pickup {
        std::unique_ptr<Entity> entity;
        int count = 1;
    };
    void recordPickup(const Entity& e, int count);
    std::vector<Pickup> takePickups() { return std::exchange(pickups_, {}); }
    // A saved entity ({"type": ..., "pos": ...}), or null for an unknown type.
    std::unique_ptr<Entity> loadEntity(const Json& j);
    static Json saveEntity(const Entity& e);

    int64_t gameTime() const { return gameTime_; }
    int64_t dayTime() const { return dayTime_; }
    void setDayTime(int64_t t) { dayTime_ = t; }
    // Game rules (vanilla names in /gamerule): doDaylightCycle, keepInventory,
    // naturalRegeneration, randomTickSpeed, doFireTick, waterSourceConversion,
    // lavaSourceConversion, doTileDrops, doEntityDrops, doVinesSpread.
    bool daylightCycle = true;
    bool keepInventory = false;
    bool naturalRegeneration = true;
    int randomTickSpeed = 3;
    bool doFireTick = true;
    bool waterSourceConversion = true;
    bool lavaSourceConversion = false;
    bool doTileDrops = true;
    bool doEntityDrops = true; // entities leave items (falling blocks that can't land)
    bool doVinesSpread = true;
    bool doMobSpawning = true;
    bool doMobLoot = true;    // mobs drop loot and experience
    bool mobGriefing = true;  // creepers break blocks, sheep eat grass, mobs trample farmland
    // A game rule by its vanilla name ("doMobSpawning"): its value as text, or nothing for
    // an unknown rule; setting checks the value (true/false, or a number for the counters).
    std::optional<std::string> gameRule(std::string_view name) const;
    bool setGameRule(std::string_view name, std::string_view value, std::string* error = nullptr);
    static std::vector<std::string_view> gameRuleNames();
    // The world's difficulty (fire spreads faster on harder ones).
    Difficulty difficulty = Difficulty::Normal;

    // World generation's features and biomes, for saplings and bone meal (without them
    // saplings don't grow into trees).
    void setWorldgen(const BiomeDecorator* decorator, const BiomeRegistry* biomes) {
        decorator_ = decorator;
        biomes_ = biomes;
    }
    const BiomeDecorator* decorator() const { return decorator_; }
    const BiomeRegistry* biomes() const { return biomes_; }
    BiomeId biomeAt(const BlockPos& p) const;
    // Vanilla skyDarken: sky light lost to night and weather (0 by day .. 11 at night).
    int skyDarken() const;
    // Vanilla isBrightOutside (isDay): the sky darkened by less than 4.
    bool isBrightOutside() const { return skyDarken() < 4; }
    // Vanilla canSeeSky: full sky light at p.
    bool canSeeSky(const BlockPos& p) const { return (lightAt(p) >> 4) >= 15; }

    // Weather (vanilla ServerLevelData's weather times and ServerLevel.advanceWeatherCycle):
    // clear for clearWeatherTime ticks, else rain and thunder each toggle when their timer
    // runs out; the levels fade 0.01 a tick toward the state.
    bool doWeatherCycle = true;
    int snowAccumulationHeight = 1; // layers snowfall builds up to
    int clearWeatherTime = 0, rainTime = 0, thunderTime = 0;
    bool raining = false, thundering = false;
    float rainLevel(float partialTick) const;
    float thunderLevel(float partialTick) const; // includes the rain level
    bool isRaining() const { return rainLevel(1.0f) > 0.2f; }
    bool isThundering() const { return thunderLevel(1.0f) > 0.9f; }
    // Vanilla setWeatherParameters (/weather clear|rain|thunder).
    void setWeather(int clearTime, int weatherTime, bool rain, bool thunder);
    // The rain and thunder levels at once (as a world loaded in the rain starts).
    void settleWeather() {
        rain_ = oRain_ = raining ? 1.0f : 0.0f;
        thunder_ = oThunder_ = raining && thundering ? 1.0f : 0.0f;
    }
    enum class Precipitation : uint8_t { None, Rain, Snow };
    // Vanilla Biome.getPrecipitationAt: none in dry biomes, snow where it's cold enough.
    Precipitation precipitationAt(const BlockPos& p) const;
    // Rain falling on `p`: raining, open to the sky, and a rainy (not snowy) biome there.
    bool isRainingAt(const BlockPos& p) const;
    // One above the highest block that blocks motion or holds fluid (vanilla MOTION_BLOCKING).
    int motionBlockingHeight(int x, int z) const;
    // Vanilla findLightningTargetAround and the strike: on the surface of the column, or on
    // the player if under the open sky within 3 blocks of it; only where rain falls.
    void spawnLightning(const BlockPos& column, bool visualOnly);
    // Vanilla getRawBrightness: the brighter of block light and sky light less `darken`.
    int rawBrightness(const BlockPos& p, int darken) const;

private:
    const BlockRegistry& registry_;
    ChunkManager chunks_;
    int64_t seed_;
    // Vanilla LevelTicks: due ticks by (time, order scheduled), one per position and type.
    class TickQueue {
    public:
        struct Tick {
            int64_t time;
            uint64_t seq;
            BlockPos pos;
            uint16_t type;
            bool operator>(const Tick& o) const { return time != o.time ? time > o.time : seq > o.seq; }
        };
        bool schedule(const BlockPos& p, uint16_t type, int64_t time);
        bool contains(const BlockPos& p, uint16_t type) const { return pending_.contains(Key{p, type}); }
        bool due(int64_t now) const { return !heap_.empty() && heap_.front().time <= now; }
        Tick pop();
        size_t size() const { return heap_.size(); }
        // Those in chunk `c`, in order; `remove` takes them out (the chunk unloads).
        std::vector<Tick> inChunk(ChunkPos c, bool remove);
        bool anyInChunk(ChunkPos c) const { return perChunk_.contains(c); }

    private:
        struct Key {
            BlockPos pos;
            uint16_t type;
            bool operator==(const Key& o) const { return pos == o.pos && type == o.type; }
        };
        struct KeyHash {
            size_t operator()(const Key& k) const { return std::hash<BlockPos>()(k.pos) * 31u + k.type; }
        };
        void counted(const BlockPos& p, int delta);
        std::vector<Tick> heap_; // min-heap
        std::unordered_set<Key, KeyHash> pending_;
        std::unordered_map<ChunkPos, int> perChunk_; // pending ticks per chunk (for saving)
        uint64_t seq_ = 0;
    };
    void runScheduledTicks();
    // Random ticks in every ticking chunk (vanilla tickChunk), and post-processing of chunks
    // that just started ticking.
    void tickChunks();
    void tickEntities();
    // Vanilla Player.aiStep touch: items and orbs within the player's box grown by (1, 0.5, 1).
    void touchPlayer();

    TickQueue blockTicks_, fluidTicks_;
    int64_t gameTime_ = 0;
    int32_t randValue_ = 0x2D4E62F1; // vanilla ServerLevel.randValue: the random tick positions
    float rain_ = 0.0f, oRain_ = 0.0f, thunder_ = 0.0f, oThunder_ = 0.0f;
    void advanceWeather();
    // Vanilla ServerLevel.tickPrecipitation: water freezing, snow piling up, cauldrons filling.
    void tickPrecipitation(const BlockPos& column);
    BlockPos randomPosIn(ChunkPos c, int y);
    std::vector<LevelEvent> events_;
    int64_t dayTime_ = 6000; // noon

    std::vector<std::unique_ptr<Entity>> entities_;
    std::unordered_map<uint32_t, Entity*> byId_;
    void dropRemoved(); // erases removed entities (and their ids)
    const EntityTypes* entityTypes_ = nullptr;
    std::unordered_map<BlockPos, std::shared_ptr<BlockEntity>> blockEntities_;
    const RecipeBook* recipes_ = nullptr;
    void tickBlockEntities();
    uint32_t nextEntityId_ = 1;
    std::unordered_set<ChunkPos> savedEntityChunks_; // chunks whose save file lists entities
    std::unordered_set<ChunkPos> populatedChunks_;   // loaded chunks that got animals when generated
    Player* player_ = nullptr;
    const ItemRegistry* items_ = nullptr;
    const BiomeDecorator* decorator_ = nullptr;
    const BiomeRegistry* biomes_ = nullptr;
    const BlockLoot* loot_ = nullptr;
    Xoroshiro random_;
    std::vector<Pickup> pickups_;
};

} // namespace mc
