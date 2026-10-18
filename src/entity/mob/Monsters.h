// Monsters.h -- the hostile mobs (vanilla Monster, Zombie, Skeleton, Creeper,
// Spider): dark-loving, despawning, burning by day (the undead), with their
// attacks: zombie melee, skeleton bow, creeper fuse and blast, spider leaps and
// wall climbing.
#pragma once

#include <memory>

#include "entity/mob/Mob.h"

namespace mc {

class Monster : public Mob {
public:
    explicit Monster(const EntityTypeInfo& type);
    float walkTargetValue(const Level& level, const BlockPos& p) const override;
    bool despawnsInPeaceful() const override { return true; }

protected:
    void mobAiStep(Level& level) override;
    int experienceReward() override;
    // The undead's daylight burning (vanilla Zombie/AbstractSkeleton.aiStep): a helmet
    // takes it (and wears) instead.
    void burnInDaylight(Level& level);
    int xpReward_ = 5;
};

// Vanilla Monster.isDarkEnoughToSpawn: sky light under a random 0..31, no block light, and
// the darkness (in thunder, the sky as at night) at most a random 0..7.
bool isDarkEnoughToSpawn(const Level& level, const BlockPos& p, Xoroshiro& random);

class Zombie : public Monster {
public:
    explicit Zombie(const EntityTypeInfo& type);
    bool baby = false;
    bool isBaby() const override { return baby; }
    void setBaby(bool b);
    void finalizeSpawn(Level& level, SpawnReason reason, SpawnGroup& group) override;
    bool doHurtTarget(Level& level, LivingEntity& target) override;
    void saveExtra(Json& out) const override;
    void loadExtra(const Json& in, const ItemRegistry& items) override;

protected:
    void mobAiStep(Level& level) override;
    int experienceReward() override;
};

class Skeleton : public Monster {
public:
    explicit Skeleton(const EntityTypeInfo& type);
    ~Skeleton() override;
    bool holdingBow() const;
    void finalizeSpawn(Level& level, SpawnReason reason, SpawnGroup& group) override;
    // Vanilla performRangedAttack: an arrow at the target's lower third, aimed above it by
    // the distance, its spread by difficulty.
    void performRangedAttack(Level& level, LivingEntity& target, float power);
    // Vanilla reassessWeaponGoal: the bow goal while holding a bow, else melee.
    void reassessWeaponGoal(Level& level);
    void loadExtra(const Json& in, const ItemRegistry& items) override;

protected:
    void mobAiStep(Level& level) override;
    void customServerAiStep(Level& level) override;

private:
    std::unique_ptr<Goal> bowGoal_, meleeGoal_;
    bool reassess_ = false;
};

class Creeper : public Monster {
public:
    explicit Creeper(const EntityTypeInfo& type);
    int swell = 0, oldSwell = 0;
    int swellDir = -1;
    int maxSwell = 30;
    int explosionRadius = 3;
    bool powered = false; // struck by lightning: twice the blast
    bool ignited = false; // lit with flint and steel: no going back
    // Vanilla getSwelling: 0..1 toward the blast.
    float swelling(float partialTick) const {
        return (float(oldSwell) + float(swell - oldSwell) * partialTick) / float(maxSwell - 2);
    }
    void tick(Level& level) override;
    bool interact(Level& level, Player& player, ItemStack& held) override;
    void thunderHit(Level& level) override;
    void saveExtra(Json& out) const override;
    void loadExtra(const Json& in, const ItemRegistry& items) override;

protected:
    void onLanded(double fallDistance) override;

private:
    void explode(Level& level);
};

class Spider : public Monster {
public:
    explicit Spider(const EntityTypeInfo& type);
    bool climbing = false; // against a wall: climbs it (vanilla isClimbing)
    void tick(Level& level) override;
    bool ignoresCobwebs() const override { return true; }

protected:
    bool onClimbable(const Level& level) const override {
        (void)level;
        return climbing;
    }
};

} // namespace mc
