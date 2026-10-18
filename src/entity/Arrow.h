// Arrow.h -- arrows (vanilla AbstractArrow / Arrow): fly with drag and gravity, hit
// the first entity or block in their way, hurt by speed x damage (critical ones a
// little more), stick in blocks shaking, and are picked up by the player when allowed.
#pragma once

#include "entity/Entity.h"
#include "game/Inventory.h"

namespace mc {

class LivingEntity;
class Player;
class Xoroshiro;

class Arrow : public Entity {
public:
    enum class Pickup : uint8_t { Disallowed, Allowed, CreativeOnly };

    Arrow();
    std::string_view type() const override { return "minecraft:arrow"; }
    void tick(Level& level) override;
    void saveExtra(Json& out) const override;
    void loadExtra(const Json& in, const ItemRegistry& items) override;
    std::unique_ptr<Entity> clone() const override { return std::make_unique<Arrow>(*this); }
    // The player walking into a stuck arrow picks it up (if it may).
    void playerTouch(Level& level, Player& player);

    // Vanilla shoot: along (x, y, z) at `speed` blocks a tick, spread by `inaccuracy`.
    void shoot(const glm::dvec3& direction, float speed, float inaccuracy, Xoroshiro& random);
    // Vanilla Projectile.shootFromRotation: along a look direction, plus the shooter's motion.
    void shootFromRotation(const LivingEntity& shooter, float pitch, float yaw, float speed, float inaccuracy,
                           Xoroshiro& random);

    uint32_t owner = 0;
    double baseDamage = 2.0;
    bool crit = false;
    bool inGround = false;
    int inGroundTime = 0, life = 0, shakeTime = 0;
    Pickup pickup = Pickup::Disallowed;
    ItemStack pickupItem;
    StateId lastState = 0; // the block it stuck in
    bool leftOwner = false;

private:
    bool canHit(const Level& level, const Entity& e) const;
    void hitBlock(Level& level, const glm::dvec3& at, const BlockPos& block);
    // True when the arrow is gone (it hurt what it hit).
    bool hitEntity(Level& level, Entity& target);
};

} // namespace mc
