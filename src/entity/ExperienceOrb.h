// ExperienceOrb.h -- experience lying in the world (vanilla ExperienceOrb): it falls,
// bounces, drifts toward a player within 8 blocks, merges with equal orbs, and gives
// its points on touch, one orb every 2 ticks.
#pragma once

#include "entity/Entity.h"

namespace mc {

class Player;

class ExperienceOrb : public Entity {
public:
    static constexpr int kLifetime = 6000;

    ExperienceOrb();
    std::string_view type() const override { return "minecraft:experience_orb"; }
    void tick(Level& level) override;
    bool hurtServer(Level& level, const DamageSource& source, float amount) override;
    void saveExtra(Json& out) const override;
    void loadExtra(const Json& in, const ItemRegistry& items) override;
    std::unique_ptr<Entity> clone() const override { return std::make_unique<ExperienceOrb>(*this); }
    void playerTouch(Level& level, Player& player);

    int value = 1; // points per orb
    int count = 1; // merged orbs of this value
    int age = 0;
    int health = 5;

    // Which of the 11 orb sprites (vanilla getIcon), by value.
    int icon() const;
    // Vanilla ExperienceOrb.award: `amount` points split into orbs of vanilla's sizes.
    static void award(Level& level, const glm::dvec3& at, int amount);
    // The largest orb size that fits in `amount` (2477, 1237, ..., 3, 1).
    static int splitValue(int amount);
};

} // namespace mc
