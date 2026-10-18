// FallingBlockEntity.h -- a block falling as an entity (vanilla FallingBlock and
// FallingBlockEntity): sand, gravel, concrete powder, anvils and the dragon egg drop when
// nothing holds them up, and land as the block again where they can, or as an item where
// they can't. Concrete powder sets on touching water; anvils hurt what they land on and
// chip.
#pragma once

#include "entity/Entity.h"

namespace mc {

class FallingBlockEntity : public Entity {
public:
    explicit FallingBlockEntity(const BlockRegistry& registry);
    std::string_view type() const override { return "minecraft:falling_block"; }
    void tick(Level& level) override;
    void saveExtra(Json& out) const override;
    void loadExtra(const Json& in, const ItemRegistry& items) override;
    std::unique_ptr<Entity> clone() const override { return std::make_unique<FallingBlockEntity>(*this); }

    // Vanilla FallingBlockEntity.fall: the block at `pos` starts falling, leaving its
    // fluid (or air) behind.
    static FallingBlockEntity& fall(Level& level, const BlockPos& pos, StateId state);
    // Vanilla FallingBlock.isFree: whether a falling block passes through `state` (air,
    // fire, fluids, replaceable plants and snow).
    static bool isFree(const BlockRegistry& registry, StateId state);
    // The kinds of blocks that fall.
    enum class Kind : uint8_t { None, Plain, ConcretePowder, Anvil, Brushable };
    static Kind kindOf(const Block& block);
    // Vanilla ConcretePowderBlock.touchesLiquid: water beside it or above (through a face
    // that isn't sturdy toward it).
    static bool touchesWater(const BlockGetter& level, const BlockPos& pos);
    // The concrete a powder sets into.
    static StateId concreteOf(const BlockRegistry& registry, StateId powder);

    StateId state = 0;
    int time = 0;              // ticks fallen
    bool dropItem = true;      // leaves its item where it can't land
    bool cancelDrop = false;   // breaks on landing (suspicious blocks, a broken anvil)
    bool hurtEntities = false; // anvils
    float fallDamagePerDistance = 0.0f;
    int fallDamageMax = 40;
    BlockPos startPos{0, 0, 0}; // where it began falling (its model variant)

private:
    const BlockRegistry* registry_;
    void causeFallDamage(Level& level, double fallDistance);
    void land(Level& level, const BlockPos& at, StateId replaced);
    void breakAfterFall(Level& level, const BlockPos& at);
    void spawnItem(Level& level);
};

} // namespace mc
