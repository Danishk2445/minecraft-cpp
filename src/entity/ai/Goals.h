// Goals.h -- vanilla's common mob goals (net.minecraft.world.entity.ai.goal) and the
// random positions they wander to (RandomPos, DefaultRandomPos, LandRandomPos), and
// who a mob may target (TargetingConditions).
#pragma once

#include <functional>
#include <optional>
#include <string>

#include <glm/glm.hpp>

#include "core/Math.h"
#include "entity/ai/Goal.h"
#include "entity/path/PathFinder.h"
#include "game/Damage.h"

namespace mc {

class Entity;
class LivingEntity;
class Mob;
class Player;

// Vanilla TargetingConditions.test: alive and seeable (combat: not creative, not on
// Peaceful), within `range` (less for sneaking players and wearers of the mob's own
// head), in sight unless `ignoreSight`.
bool canTarget(Level& level, Mob& mob, const LivingEntity& target, double range, bool combat, bool ignoreSight);

// Random spots near the mob (vanilla RandomPos.generateRandomPos with 10 tries, the one it
// likes best by its walk target value): any stable, malus-free spot, or one on land.
std::optional<glm::dvec3> defaultRandomPos(Level& level, Mob& mob, int radius, int vertical);
std::optional<glm::dvec3> landRandomPos(Level& level, Mob& mob, int radius, int vertical);

// Vanilla DamageTypeTags.PANIC_CAUSES: attacks and the hazards that hurt.
bool causesPanic(DamageType type);

class FloatGoal : public Goal {
public:
    explicit FloatGoal(Mob& mob);
    bool canUse(Level& level) override;
    bool requiresUpdateEveryTick() const override { return true; }
    void tick(Level& level) override;

private:
    Mob& mob_;
};

class PanicGoal : public Goal {
public:
    PanicGoal(Mob& mob, double speed) : mob_(mob), speed_(speed) { flags = kGoalMove; }
    bool canUse(Level& level) override;
    bool canContinueToUse(Level& level) override;
    void start(Level& level) override;
    void stop(Level& level) override;

protected:
    bool shouldPanic() const;
    Mob& mob_;
    double speed_;
    glm::dvec3 wanted_{0.0};
};

class RandomStrollGoal : public Goal {
public:
    RandomStrollGoal(Mob& mob, double speed, int interval = 120, bool checkNoActionTime = true)
        : mob_(mob), speed_(speed), interval_(interval), checkNoActionTime_(checkNoActionTime) {
        flags = kGoalMove;
    }
    bool canUse(Level& level) override;
    bool canContinueToUse(Level& level) override;
    void start(Level& level) override;
    void stop(Level& level) override;
    void trigger() { forceTrigger_ = true; }

protected:
    virtual std::optional<glm::dvec3> position(Level& level);
    Mob& mob_;
    double speed_;
    int interval_;
    bool checkNoActionTime_, forceTrigger_ = false;
    glm::dvec3 wanted_{0.0};
};

class WaterAvoidingRandomStrollGoal : public RandomStrollGoal {
public:
    WaterAvoidingRandomStrollGoal(Mob& mob, double speed, float probability = 0.001f)
        : RandomStrollGoal(mob, speed), probability_(probability) {}

protected:
    std::optional<glm::dvec3> position(Level& level) override;
    float probability_;
};

class LookAtPlayerGoal : public Goal {
public:
    LookAtPlayerGoal(Mob& mob, float distance, float probability = 0.02f)
        : mob_(mob), distance_(distance), probability_(probability) {
        flags = kGoalLook;
    }
    bool canUse(Level& level) override;
    bool canContinueToUse(Level& level) override;
    void start(Level& level) override;
    void stop(Level& level) override;
    void tick(Level& level) override;

private:
    Mob& mob_;
    float distance_, probability_;
    uint32_t lookAt_ = 0;
    int lookTime_ = 0;
};

class RandomLookAroundGoal : public Goal {
public:
    explicit RandomLookAroundGoal(Mob& mob) : mob_(mob) { flags = kGoalMove | kGoalLook; }
    bool canUse(Level& level) override;
    bool canContinueToUse(Level& level) override { (void)level; return lookTime_ >= 0; }
    void start(Level& level) override;
    bool requiresUpdateEveryTick() const override { return true; }
    void tick(Level& level) override;

private:
    Mob& mob_;
    double relX_ = 0.0, relZ_ = 0.0;
    int lookTime_ = 0;
};

// Follows a player holding one of the items of `itemTag` (vanilla TemptGoal).
class TemptGoal : public Goal {
public:
    TemptGoal(Mob& mob, double speed, std::string itemTag) : mob_(mob), speed_(speed), tag_(std::move(itemTag)) {
        flags = kGoalMove | kGoalLook;
    }
    bool canUse(Level& level) override;
    void start(Level& level) override { (void)level; running_ = true; }
    void stop(Level& level) override;
    void tick(Level& level) override;
    bool running() const { return running_; }

private:
    bool tempting(const Level& level, const Player& p) const;
    Mob& mob_;
    double speed_;
    std::string tag_;
    int calmDown_ = 0;
    bool running_ = false;
};

// Chases the target and hits it in reach (vanilla MeleeAttackGoal).
class MeleeAttackGoal : public Goal {
public:
    MeleeAttackGoal(Mob& mob, double speed, bool followEvenIfNotSeen)
        : mob_(mob), speed_(speed), followUnseen_(followEvenIfNotSeen) {
        flags = kGoalMove | kGoalLook;
    }
    bool canUse(Level& level) override;
    bool canContinueToUse(Level& level) override;
    void start(Level& level) override;
    void stop(Level& level) override;
    bool requiresUpdateEveryTick() const override { return true; }
    void tick(Level& level) override;
    int ticksUntilNextAttack() const { return ticksUntilNextAttack_; }
    int attackInterval() const { return adjustedTickDelay(20); }

protected:
    void checkAndPerformAttack(Level& level, LivingEntity& target);
    Mob& mob_;
    double speed_;
    bool followUnseen_;
    std::optional<Path> path_;
    glm::dvec3 pathedTarget_{0.0};
    int ticksUntilNextPathRecalculation_ = 0, ticksUntilNextAttack_ = 0;
    int64_t lastCanUseCheck_ = -100;
};

// Targets whoever hurt it last (vanilla HurtByTargetGoal).
class HurtByTargetGoal : public Goal {
public:
    explicit HurtByTargetGoal(Mob& mob) : mob_(mob) { flags = kGoalTarget; }
    bool canUse(Level& level) override;
    bool canContinueToUse(Level& level) override;
    void start(Level& level) override;
    void stop(Level& level) override;

private:
    Mob& mob_;
    int timestamp_ = 0, unseenTicks_ = 0;
};

// Targets the nearest player it sees (vanilla NearestAttackableTargetGoal<Player>).
class NearestAttackableTargetGoal : public Goal {
public:
    NearestAttackableTargetGoal(Mob& mob, bool mustSee, std::function<bool(Level&)> condition = {})
        : mob_(mob), mustSee_(mustSee), condition_(std::move(condition)) {
        flags = kGoalTarget;
    }
    bool canUse(Level& level) override;
    bool canContinueToUse(Level& level) override;
    void start(Level& level) override;
    void stop(Level& level) override;

private:
    Mob& mob_;
    bool mustSee_;
    std::function<bool(Level&)> condition_;
    uint32_t found_ = 0;
    int unseenTicks_ = 0;
};

// Jumps at a target 2 to 4 blocks away (vanilla LeapAtTargetGoal).
class LeapAtTargetGoal : public Goal {
public:
    LeapAtTargetGoal(Mob& mob, float yd) : mob_(mob), yd_(yd) { flags = kGoalJump | kGoalMove; }
    bool canUse(Level& level) override;
    bool canContinueToUse(Level& level) override;
    void start(Level& level) override;

private:
    Mob& mob_;
    float yd_;
    uint32_t target_ = 0;
};

// Undead without helmets: paths avoid the open sky by day (vanilla RestrictSunGoal) and a
// burning one runs for shade (FleeSunGoal).
class RestrictSunGoal : public Goal {
public:
    explicit RestrictSunGoal(Mob& mob) : mob_(mob) {}
    bool canUse(Level& level) override;
    void start(Level& level) override;
    void stop(Level& level) override;

private:
    Mob& mob_;
};

class FleeSunGoal : public Goal {
public:
    FleeSunGoal(Mob& mob, double speed) : mob_(mob), speed_(speed) { flags = kGoalMove; }
    bool canUse(Level& level) override;
    bool canContinueToUse(Level& level) override;
    void start(Level& level) override;

private:
    Mob& mob_;
    double speed_;
    glm::dvec3 wanted_{0.0};
};

} // namespace mc
