// Animals.h -- the farm animals (vanilla Animal / AgeableMob, Pig, Cow, Sheep,
// Chicken): growing up, being fed and bred, following parents and food; sheep
// wool (colors, shearing, regrowing by eating grass), chickens laying eggs.
#pragma once

#include "entity/mob/Mob.h"

namespace mc {

// Vanilla's farm animal climates (1.21.5): the biome tags spawns_warm_variant_farm_animals
// and spawns_cold_variant_farm_animals. Pigs and chickens come warm or temperate (cold ones
// need other geometry and stay temperate); sheep take their wool colors from it.
enum class FarmVariant : uint8_t { Temperate, Warm, Cold };
FarmVariant farmVariantAt(const Level& level, const BlockPos& p);

class Animal : public Mob {
public:
    explicit Animal(const EntityTypeInfo& type);

    // Vanilla AgeableMob age: below 0 a baby (growing toward 0), above 0 the time until it
    // may breed again.
    int age = 0;
    int inLove = 0; // ticks left wanting to breed
    static constexpr int kBabyAge = -24000;
    bool isBaby() const override { return age < 0; }
    void setAge(int a);
    // Vanilla ageUp: `seconds` closer to grown up.
    void ageUp(int seconds);
    bool isFood(const Level& level, const ItemStack& stack) const;
    bool canFallInLove() const { return inLove <= 0; }
    void setInLove(Level& level);
    bool canMate(const Animal& other) const;
    // The baby two parents make (vanilla getBreedOffspring); null if none.
    virtual std::unique_ptr<Animal> breedOffspring(Level& level, Animal& partner);
    // Vanilla spawnChildFromBreeding: the baby, both parents' cooldown and experience.
    void spawnChildFromBreeding(Level& level, Animal& partner);

    float walkTargetValue(const Level& level, const BlockPos& p) const override;
    bool removeWhenFarAway(double distanceSq) const override {
        (void)distanceSq;
        return false;
    }
    bool checkSpawnRules(const Level& level, SpawnReason reason) const override;
    bool hurtServer(Level& level, const DamageSource& source, float amount) override;
    bool interact(Level& level, Player& player, ItemStack& held) override;
    void finalizeSpawn(Level& level, SpawnReason reason, SpawnGroup& group) override;
    void saveExtra(Json& out) const override;
    void loadExtra(const Json& in, const ItemRegistry& items) override;

protected:
    void mobAiStep(Level& level) override;
    int experienceReward() override;
    // Registers vanilla's shared animal goals around the type's own: float, panic, breed,
    // tempt, follow parent, stroll, look.
    void addAnimalGoals(double panicSpeed, double temptSpeed, double followSpeed, bool eatsGrass);
};

class Pig : public Animal {
public:
    explicit Pig(const EntityTypeInfo& type);
    FarmVariant variant = FarmVariant::Temperate;
    std::unique_ptr<Animal> breedOffspring(Level& level, Animal& partner) override;
    void finalizeSpawn(Level& level, SpawnReason reason, SpawnGroup& group) override;
    void saveExtra(Json& out) const override;
    void loadExtra(const Json& in, const ItemRegistry& items) override;
};

class Cow : public Animal {
public:
    explicit Cow(const EntityTypeInfo& type);
    // Milked with a bucket.
    bool interact(Level& level, Player& player, ItemStack& held) override;
    std::unique_ptr<Animal> breedOffspring(Level& level, Animal& partner) override;
};

class Sheep : public Animal {
public:
    explicit Sheep(const EntityTypeInfo& type);
    int color = 0; // DyeColor index
    bool sheared = false;
    // Vanilla eatAnimationTick: grazing's countdown (the goal's, every other tick), and the
    // animation's, 40 down to 0 a tick at a time (the head goes down to the grass).
    int eatAnimationTick = 0;
    int eatAnimation = 0;
    bool readyForShearing() const { return alive() && !sheared && !isBaby(); }
    // Vanilla shear: 1-3 wool of its color pop out.
    void shear(Level& level);
    // Vanilla ate: the wool grows back (and a lamb grows a little).
    void ate();
    bool interact(Level& level, Player& player, ItemStack& held) override;
    std::unique_ptr<Animal> breedOffspring(Level& level, Animal& partner) override;
    void finalizeSpawn(Level& level, SpawnReason reason, SpawnGroup& group) override;
    void saveExtra(Json& out) const override;
    void loadExtra(const Json& in, const ItemRegistry& items) override;
    // Vanilla getHeadEatPositionScale / getHeadEatAngleScale for the model.
    float headEatPosition(float partialTick) const;
    float headEatAngle(float partialTick) const;
    // Vanilla SheepColorSpawnRules: 82% the climate's common color (white, brown when warm,
    // black when cold; one in 500 of those pink), then gray, light gray and the others.
    static int randomColor(Xoroshiro& random, FarmVariant climate);

protected:
    void mobAiStep(Level& level) override;
    DeathContext deathContext() const override;
};

class Chicken : public Animal {
public:
    explicit Chicken(const EntityTypeInfo& type);
    FarmVariant variant = FarmVariant::Temperate;
    int eggTime = 6000;
    // Wing flapping (vanilla flap, flapSpeed, flapping and their previous values).
    float flap = 0.0f, oFlap = 0.0f, flapSpeed = 0.0f, oFlapSpeed = 0.0f, flapping = 1.0f;
    bool fallDamageImmune() const override { return true; }
    std::unique_ptr<Animal> breedOffspring(Level& level, Animal& partner) override;
    void finalizeSpawn(Level& level, SpawnReason reason, SpawnGroup& group) override;
    void saveExtra(Json& out) const override;
    void loadExtra(const Json& in, const ItemRegistry& items) override;

protected:
    void mobAiStep(Level& level) override;
};

} // namespace mc
