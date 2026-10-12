// Level.h -- one dimension's world state: chunks, time, and block access.
#pragma once

#include "core/Json.h"
#include "world/block/BlockGetter.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/Rng.h"
#include "entity/Entity.h"
#include "world/chunk/ChunkManager.h"

namespace mc {

class BlockEntity;
class BlockLoot;
class ChunkStorage;
class ItemRegistry;
class Player;
class RecipeBook;

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

    // Entities (dropped items, experience orbs; later mobs): owned here, ticked while
    // their chunk is ticking, saved and loaded with their chunks.
    Entity& addEntity(std::unique_ptr<Entity> entity);
    const std::vector<std::unique_ptr<Entity>>& entities() const { return entities_; }
    // Live entities whose boxes intersect `box`, except `except`.
    std::vector<Entity*> entitiesIn(const AABB& box, const Entity* except = nullptr) const;
    // The player (not owned): orbs drift to it and it picks up what it touches.
    void setPlayer(Player* player) { player_ = player; }
    Player* player() const { return player_; }
    // The item registry, for loading saved entities.
    void setItems(const ItemRegistry* items) { items_ = items; }
    // Block loot: blocks a neighbor update removes (a torch losing its wall, a door's other
    // half) drop theirs, as vanilla's destroyBlock does.
    void setLoot(const BlockLoot* loot) { loot_ = loot; }
    const ItemRegistry* itemRegistry() const { return items_; }
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
    // Game rules (vanilla names in /gamerule): doDaylightCycle, keepInventory, naturalRegeneration.
    bool daylightCycle = true;
    bool keepInventory = false;
    bool naturalRegeneration = true;

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
    void tickEntities();
    // Vanilla Player.aiStep touch: items and orbs within the player's box grown by (1, 0.5, 1).
    void touchPlayer();

    std::vector<ScheduledTick> scheduled_; // min-heap by (time, seq)
    uint64_t tickSeq_ = 0;
    int64_t gameTime_ = 0;
    int64_t dayTime_ = 6000; // noon

    std::vector<std::unique_ptr<Entity>> entities_;
    std::unordered_map<BlockPos, std::shared_ptr<BlockEntity>> blockEntities_;
    const RecipeBook* recipes_ = nullptr;
    void tickBlockEntities();
    uint32_t nextEntityId_ = 1;
    std::unordered_set<ChunkPos> savedEntityChunks_; // chunks whose save file lists entities
    Player* player_ = nullptr;
    const ItemRegistry* items_ = nullptr;
    const BlockLoot* loot_ = nullptr;
    Xoroshiro random_;
    std::vector<Pickup> pickups_;
};

} // namespace mc
