// ItemEntity.h -- an item stack lying in the world (vanilla ItemEntity): it falls, slides
// with the floor's friction, floats in water, merges with matching stacks nearby, is
// picked up by the player after its pickup delay, and despawns after five minutes.
#pragma once

#include "entity/Entity.h"
#include "game/Inventory.h"

namespace mc {

class Player;

class ItemEntity : public Entity {
public:
    static constexpr int kLifetime = 6000;    // ticks before it despawns
    static constexpr int kNeverPickUp = 32767; // vanilla INFINITE_PICKUP_DELAY

    ItemEntity();
    std::string_view type() const override { return "minecraft:item"; }
    void tick(Level& level) override;
    void saveExtra(Json& out) const override;
    void loadExtra(const Json& in, const ItemRegistry& items) override;
    std::unique_ptr<Entity> clone() const override { return std::make_unique<ItemEntity>(*this); }
    // Vanilla playerTouch: into the inventory once the pickup delay is over.
    void playerTouch(Level& level, Player& player);

    ItemStack stack;
    int age = 0;
    int pickupDelay = 0;
    int health = 5;
    float bobOffset = 0.0f; // radians, random per entity (rendering)

    // Vanilla Block.popResource: a stack popping out of a broken block at `pos`.
    static void popResource(Level& level, const BlockPos& pos, const ItemStack& stack);
    // Vanilla Player.drop: thrown from the eyes along the look (or scattered around the
    // player when it dies, `around`); the thrower can't pick it up for 2 seconds.
    static void dropFromPlayer(Level& level, const Player& player, const ItemStack& stack, bool around = false);
    // A stack at a point with vanilla's default random toss (containers spilling, ...).
    static ItemEntity& spawn(Level& level, const glm::dvec3& at, const ItemStack& stack);
    // Vanilla Containers.dropItemStack: a broken container's stack, in clumps of 10 to 30.
    static void spill(Level& level, const BlockPos& pos, ItemStack stack);

private:
    bool mergeable() const;
    void mergeWithNeighbours(Level& level);
    void tryToMerge(ItemEntity& other);
};

// Whether lava and fire spare this item (vanilla's damage_resistant: is_fire, netherite).
bool fireResistant(const Item& item);

} // namespace mc
