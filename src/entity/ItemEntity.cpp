#include "entity/ItemEntity.h"

#include <cmath>

#include "entity/Player.h"
#include "world/level/Level.h"

namespace mc {

bool fireResistant(const Item& item) {
    const std::string_view id = item.id;
    return id.find("netherite") != std::string_view::npos || id.ends_with("ancient_debris");
}

ItemEntity::ItemEntity() {
    width = height = 0.25f;
}

void ItemEntity::tick(Level& level) {
    if (stack.empty()) {
        removed = true;
        return;
    }
    ++tickCount;
    oldPos = pos;
    if (pickupDelay > 0 && pickupDelay != kNeverPickUp) --pickupDelay;
    updateFluidState(level);
    if (inWater && waterHeight > 0.1) {
        // setUnderwaterMovement: drag, and a slow rise toward the surface.
        velocity.x *= 0.99f;
        velocity.z *= 0.99f;
        if (velocity.y < 0.06f) velocity.y += 5.0e-4f;
    } else if (inLava && lavaHeight > 0.1 && fireResistant(*stack.item)) {
        velocity.x *= 0.95f;
        velocity.z *= 0.95f;
        if (velocity.y < 0.06f) velocity.y += 5.0e-4f;
    } else {
        velocity.y -= 0.04; // gravity
    }
    noPhysics = insideBlocks(level);
    if (noPhysics) moveTowardsClosestSpace(level, level.random().nextFloat());
    // Resting items only move every 4th tick (vanilla's (tickCount + id) % 4).
    if (!onGround || velocity.x * velocity.x + velocity.z * velocity.z > 1.0e-5 || (tickCount + int(id)) % 4 == 0) {
        move(level, velocity);
        float friction = 0.98f;
        if (onGround) friction = level.registry().blockOf(level.getBlock(blockBelow())).friction * 0.98f;
        velocity *= glm::dvec3(friction, 0.98, friction);
        if (onGround && velocity.y < 0.0) velocity.y *= -0.5;
    }
    // Merging: every 2 ticks while it moves between blocks, else every 40.
    const bool newBlock = BlockPos::containing(oldPos) != BlockPos::containing(pos);
    if (tickCount % (newBlock ? 2 : 40) == 0 && mergeable()) mergeWithNeighbours(level);
    if (removed) return; // merged into a neighbor
    if (age != -32768) ++age;
    // Lava and fire burn it (health 5); cactus pricks it.
    const StateId at = level.getBlock(BlockPos::containing(pos + glm::dvec3(0.0, 0.1, 0.0)));
    const std::string_view atId = level.registry().blockOf(at).id;
    if (!fireResistant(*stack.item) && (inLava || atId.ends_with(":fire") || atId.ends_with("soul_fire"))) health = 0;
    if (atId.ends_with(":cactus")) health -= 1;
    if (health <= 0 || age >= kLifetime || pos.y < double(kWorldMinY - 64)) removed = true;
}

bool ItemEntity::hurtServer(Level& level, const DamageSource& source, float amount) {
    // Netherite and the like don't burn; mobs' blasts spare items unless mobs may grief.
    if (source.info().fire && fireResistant(*stack.item)) return false;
    if (!level.mobGriefing && source.attacker && !source.attacker->isPlayer()) return false;
    health = int(float(health) - amount);
    if (health <= 0) removed = true;
    return true;
}

bool ItemEntity::mergeable() const {
    return !removed && pickupDelay != kNeverPickUp && age != -32768 && age < kLifetime && stack.count < stack.maxStack();
}

void ItemEntity::mergeWithNeighbours(Level& level) {
    for (Entity* e : level.entitiesIn(box().inflated(0.5, 0.0, 0.5), this)) {
        auto* other = dynamic_cast<ItemEntity*>(e);
        if (!other || !other->mergeable()) continue;
        tryToMerge(*other);
        if (removed) break;
    }
}

void ItemEntity::tryToMerge(ItemEntity& other) {
    // areMergable: the same item and data, and the two fit in one stack.
    if (!stack.sameItem(other.stack) || stack.count + other.stack.count > stack.maxStack()) return;
    // The smaller stack flows into the larger.
    ItemEntity& to = other.stack.count < stack.count ? *this : other;
    ItemEntity& from = &to == this ? other : *this;
    const int moved = std::min(to.stack.maxStack() - to.stack.count, from.stack.count);
    to.stack.count += moved;
    from.stack.shrink(moved);
    to.pickupDelay = std::max(to.pickupDelay, from.pickupDelay);
    to.age = std::min(to.age, from.age);
    if (from.stack.empty()) from.removed = true;
}

void ItemEntity::playerTouch(Level& level, Player& player) {
    if (pickupDelay != 0 || removed) return;
    const int before = stack.count;
    player.inventory.add(stack);
    const int taken = before - stack.count;
    if (taken <= 0) return;
    level.recordPickup(*this, taken);
    if (stack.empty()) removed = true;
}

void ItemEntity::saveExtra(Json& out) const {
    out["item"] = stackToJson(stack);
    out["age"] = age;
    out["pickup_delay"] = pickupDelay;
    out["health"] = health;
}

void ItemEntity::loadExtra(const Json& in, const ItemRegistry& items) {
    stack = stackFromJson(in.value("item", Json()), items);
    age = in.value("age", 0);
    pickupDelay = in.value("pickup_delay", 0);
    health = in.value("health", 5);
}

ItemEntity& ItemEntity::spawn(Level& level, const glm::dvec3& at, const ItemStack& stack) {
    auto e = std::make_unique<ItemEntity>();
    e->pos = e->oldPos = at;
    e->stack = stack;
    Xoroshiro& r = level.random();
    e->yaw = r.nextFloat() * 360.0f;
    e->bobOffset = r.nextFloat() * 3.1415927f * 2.0f;
    e->velocity = {r.nextDouble() * 0.2 - 0.1, 0.2, r.nextDouble() * 0.2 - 0.1};
    return static_cast<ItemEntity&>(level.addEntity(std::move(e)));
}

void ItemEntity::popResource(Level& level, const BlockPos& pos, const ItemStack& stack) {
    if (stack.empty()) return;
    Xoroshiro& r = level.random();
    const double halfHeight = 0.125; // EntityType.ITEM height / 2
    const glm::dvec3 at(pos.x + 0.5 + (r.nextDouble() * 0.5 - 0.25), pos.y + 0.5 + (r.nextDouble() * 0.5 - 0.25) - halfHeight,
                        pos.z + 0.5 + (r.nextDouble() * 0.5 - 0.25));
    spawn(level, at, stack).pickupDelay = 10; // setDefaultPickUpDelay
}

void ItemEntity::spill(Level& level, const BlockPos& pos, ItemStack stack) {
    Xoroshiro& r = level.random();
    const double w = 0.25, room = 1.0 - w; // EntityType.ITEM width
    const glm::dvec3 at(pos.x + r.nextDouble() * room + w / 2, pos.y + r.nextDouble() * room,
                        pos.z + r.nextDouble() * room + w / 2);
    // RandomSource.triangle(mode, deviation): mode + deviation * (nextDouble - nextDouble).
    auto triangle = [&](double mode, double deviation) { return mode + deviation * (r.nextDouble() - r.nextDouble()); };
    while (!stack.empty()) {
        ItemEntity& e = spawn(level, at, stack.split(r.nextInt(21) + 10));
        e.velocity = {triangle(0.0, 0.11485000171139836), triangle(0.2, 0.11485000171139836),
                      triangle(0.0, 0.11485000171139836)};
    }
}

void ItemEntity::dropFromPlayer(Level& level, const Player& player, const ItemStack& stack, bool around) {
    if (stack.empty()) return;
    ItemEntity& e = spawn(level, player.pos + glm::dvec3(0.0, player.eyeHeight() - 0.3, 0.0), stack);
    Xoroshiro& r = level.random();
    if (around) {
        // Death: scattered in every direction.
        const float speed = r.nextFloat() * 0.5f, angle = r.nextFloat() * 6.2831855f;
        e.velocity = {-std::sin(angle) * speed, 0.2, std::cos(angle) * speed};
        e.pickupDelay = 40;
        return;
    }
    const float pitch = glm::radians(player.pitch), yaw = glm::radians(player.yaw);
    const float sp = std::sin(pitch), cp = std::cos(pitch), sy = std::sin(yaw), cy = std::cos(yaw);
    const float angle = r.nextFloat() * 6.2831855f, spread = 0.02f * r.nextFloat();
    const float up = (r.nextFloat() - r.nextFloat()) * 0.1f;
    e.velocity = {-sy * cp * 0.3f + std::cos(angle) * spread, -sp * 0.3f + 0.1f + up, cy * cp * 0.3f + std::sin(angle) * spread};
    e.pickupDelay = 40;
}

} // namespace mc
