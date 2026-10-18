#include "entity/mob/Monsters.h"

#include <cmath>

#include "entity/Arrow.h"
#include "entity/Player.h"
#include "entity/ai/Goals.h"
#include "world/level/Level.h"
#include "world/sim/Explosion.h"

namespace mc {
namespace {

double distanceSq(const Entity& a, const Entity& b) {
    const glm::dvec3 d = a.pos - b.pos;
    return glm::dot(d, d);
}

// Vanilla ZombieAttackGoal: melee, the arms raised once the attack is on its way.
class ZombieAttackGoal : public MeleeAttackGoal {
public:
    ZombieAttackGoal(Zombie& zombie, double speed) : MeleeAttackGoal(zombie, speed, false) {}
    void start(Level& level) override {
        MeleeAttackGoal::start(level);
        raiseArmTicks_ = 0;
    }
    void stop(Level& level) override {
        MeleeAttackGoal::stop(level);
        mob_.aggressive = false;
    }
    void tick(Level& level) override {
        MeleeAttackGoal::tick(level);
        ++raiseArmTicks_;
        mob_.aggressive = raiseArmTicks_ >= 5 && ticksUntilNextAttack() < attackInterval() / 2;
    }

private:
    int raiseArmTicks_ = 0;
};

// Vanilla RangedBowAttackGoal: keeps 15 blocks off, strafes when it has seen the target a
// while, and draws the bow for a second before each shot.
class RangedBowAttackGoal : public Goal {
public:
    RangedBowAttackGoal(Skeleton& mob, double speed, float radius) : mob_(mob), speed_(speed), radiusSq_(radius * radius) {
        flags = kGoalMove | kGoalLook;
    }
    int minAttackInterval = 40;
    bool canUse(Level& level) override { return mob_.target(level) && mob_.holdingBow(); }
    bool canContinueToUse(Level& level) override {
        return (canUse(level) || !mob_.navigation.isDone()) && mob_.holdingBow();
    }
    void start(Level& level) override {
        (void)level;
        mob_.aggressive = true;
    }
    void stop(Level& level) override {
        (void)level;
        mob_.aggressive = false;
        seeTime_ = 0;
        attackTime_ = -1;
        mob_.stopUsingItem();
    }
    bool requiresUpdateEveryTick() const override { return true; }
    void tick(Level& level) override {
        LivingEntity* target = mob_.target(level);
        if (!target) return;
        const double d = distanceSq(mob_, *target);
        const bool sees = mob_.sensing.hasLineOfSight(level, *target);
        if (sees != (seeTime_ > 0)) seeTime_ = 0;
        seeTime_ += sees ? 1 : -1;
        if (!(d > double(radiusSq_)) && seeTime_ >= 20) {
            mob_.navigation.stop();
            ++strafingTime_;
        } else {
            mob_.navigation.moveTo(level, *target, speed_);
            strafingTime_ = -1;
        }
        if (strafingTime_ >= 20) {
            if (mob_.random.nextFloat() < 0.3f) clockwise_ = !clockwise_;
            if (mob_.random.nextFloat() < 0.3f) backwards_ = !backwards_;
            strafingTime_ = 0;
        }
        if (strafingTime_ > -1) {
            if (d > double(radiusSq_ * 0.75f)) backwards_ = false;
            else if (d < double(radiusSq_ * 0.25f)) backwards_ = true;
            mob_.moveControl.strafe(backwards_ ? -0.5f : 0.5f, clockwise_ ? 0.5f : -0.5f);
            mob_.lookAt(*target, 30.0f, 30.0f);
        } else {
            mob_.lookControl.setLookAt(*target, 30.0f, 30.0f);
        }
        if (mob_.usingItem()) {
            if (!sees && seeTime_ < -60) {
                mob_.stopUsingItem();
            } else if (sees) {
                const int t = mob_.ticksUsingItem();
                if (t >= 20) {
                    mob_.stopUsingItem();
                    // Vanilla BowItem.getPowerForTime.
                    float power = float(t) / 20.0f;
                    power = std::min((power * power + power * 2.0f) / 3.0f, 1.0f);
                    mob_.performRangedAttack(level, *target, power);
                    attackTime_ = minAttackInterval;
                }
            }
        } else if (--attackTime_ <= 0 && seeTime_ >= -60) {
            mob_.startUsingItem();
        }
    }

private:
    Skeleton& mob_;
    double speed_;
    float radiusSq_;
    int attackTime_ = -1, seeTime_ = 0, strafingTime_ = -1;
    bool clockwise_ = false, backwards_ = false;
};

// Vanilla SwellGoal: the creeper hisses with its target within 3 blocks and stops once
// it is 7 away or out of sight.
class SwellGoal : public Goal {
public:
    explicit SwellGoal(Creeper& creeper) : creeper_(creeper) { flags = kGoalMove; }
    bool canUse(Level& level) override {
        const LivingEntity* t = creeper_.target(level);
        return creeper_.swellDir > 0 || (t && distanceSq(creeper_, *t) < 9.0);
    }
    void start(Level& level) override {
        creeper_.navigation.stop();
        target_ = creeper_.targetId;
        (void)level;
    }
    void stop(Level& level) override {
        (void)level;
        target_ = 0;
    }
    bool requiresUpdateEveryTick() const override { return true; }
    void tick(Level& level) override {
        auto* t = dynamic_cast<LivingEntity*>(level.entity(target_));
        if (!t || distanceSq(creeper_, *t) > 49.0 || !creeper_.sensing.hasLineOfSight(level, *t)) creeper_.swellDir = -1;
        else creeper_.swellDir = 1;
    }

private:
    Creeper& creeper_;
    uint32_t target_ = 0;
};

// Vanilla Spider.SpiderAttackGoal: melee that gives up now and then in the light.
class SpiderAttackGoal : public MeleeAttackGoal {
public:
    explicit SpiderAttackGoal(Spider& spider) : MeleeAttackGoal(spider, 1.0, true) {}
    bool canContinueToUse(Level& level) override {
        if (lightLevel(level) >= 0.5f && mob_.random.nextInt(100) == 0) {
            mob_.setTarget(nullptr);
            return false;
        }
        return MeleeAttackGoal::canContinueToUse(level);
    }

private:
    float lightLevel(const Level& level) const {
        const float f = float(level.rawBrightness(BlockPos::containing(mob_.eyePos()), level.skyDarken())) / 15.0f;
        return f / (4.0f - 3.0f * f);
    }
};

float magicBrightness(const Level& level, const BlockPos& p) {
    const float f = float(level.rawBrightness(p, level.skyDarken())) / 15.0f;
    return f / (4.0f - 3.0f * f);
}

} // namespace

bool isDarkEnoughToSpawn(const Level& level, const BlockPos& p, Xoroshiro& random) {
    const uint8_t light = level.lightAt(p);
    if (int(light >> 4) > random.nextInt(32)) return false;
    if ((light & 15) > 0) return false; // the overworld's monster block light limit is 0
    const int darkness = level.rawBrightness(p, level.isThundering() ? 10 : level.skyDarken());
    return darkness <= random.nextInt(8);
}

// Monster

Monster::Monster(const EntityTypeInfo& type) : Mob(type) {}

float Monster::walkTargetValue(const Level& level, const BlockPos& p) const { return 0.5f - magicBrightness(level, p); }

void Monster::mobAiStep(Level& level) {
    // Vanilla updateNoActionTime: in the light it gets bored (and despawns) twice as fast.
    if (brightness(level) > 0.5f) noActionTime += 2;
}

int Monster::experienceReward() {
    int xp = xpReward_;
    for (size_t slot = 0; slot < equipment.size(); ++slot)
        if (!equipment[slot].empty() && dropChances[slot] <= 1.0f) xp += 1 + random.nextInt(3);
    return xp;
}

void Monster::burnInDaylight(Level& level) {
    if (!alive() || !isSunBurnTick(level)) return;
    ItemStack& helmet = equipment[size_t(EquipmentSlot::Head)];
    if (!helmet.empty()) {
        if (helmet.damageable() && helmet.hurt(random.nextInt(2))) helmet = ItemStack{};
        return;
    }
    igniteForTicks(160);
}

// Zombie

Zombie::Zombie(const EntityTypeInfo& type) : Monster(type) {
    goals.emplace<LookAtPlayerGoal>(8, *this, 8.0f);
    goals.emplace<RandomLookAroundGoal>(8, *this);
    goals.emplace<ZombieAttackGoal>(2, *this, 1.0);
    goals.emplace<WaterAvoidingRandomStrollGoal>(7, *this, 1.0);
    targets.emplace<HurtByTargetGoal>(1, *this);
    targets.emplace<NearestAttackableTargetGoal>(2, *this, true);
}

void Zombie::setBaby(bool b) {
    baby = b;
    refreshDimensions();
    movementSpeed = typeInfo().movementSpeed * (b ? 1.5f : 1.0f); // vanilla's baby speed bonus
}

void Zombie::finalizeSpawn(Level& level, SpawnReason reason, SpawnGroup& group) {
    Monster::finalizeSpawn(level, reason, group);
    const float special = specialMultiplier(level);
    if (!group.zombieBaby) group.zombieBaby = random.nextFloat() < 0.05f;
    if (*group.zombieBaby) setBaby(true);
    populateDefaultEquipment(level);
    // One in 100 (20 on Hard) carries an iron sword or shovel.
    if (random.nextFloat() < (level.difficulty == Difficulty::Hard ? 0.05f : 0.01f))
        if (const ItemRegistry* items = level.itemRegistry())
            if (const Item* weapon = items->find(random.nextInt(3) == 0 ? "iron_sword" : "iron_shovel"))
                setItemSlot(EquipmentSlot::MainHand, ItemStack{weapon, 1});
    knockbackResistance += float(random.nextDouble() * 0.05);
    (void)special;
}

bool Zombie::doHurtTarget(Level& level, LivingEntity& target) {
    const bool hit = Monster::doHurtTarget(level, target);
    // Burning bare-handed zombies set what they hit alight.
    if (hit && mainHand().empty() && onFire()) {
        const float effective = float(int(level.difficulty)) * 0.75f;
        if (random.nextFloat() < effective * 0.3f) target.igniteForTicks(20 * 2 * int(effective));
    }
    return hit;
}

void Zombie::mobAiStep(Level& level) {
    burnInDaylight(level);
    Monster::mobAiStep(level);
}

int Zombie::experienceReward() {
    if (baby) xpReward_ = int(5 * 2.5);
    return Monster::experienceReward();
}

void Zombie::saveExtra(Json& out) const {
    Monster::saveExtra(out);
    out["baby"] = baby;
}

void Zombie::loadExtra(const Json& in, const ItemRegistry& items) {
    Monster::loadExtra(in, items);
    setBaby(in.value("baby", false));
}

// Skeleton

Skeleton::Skeleton(const EntityTypeInfo& type) : Monster(type) {
    goals.emplace<RestrictSunGoal>(2, *this);
    goals.emplace<FleeSunGoal>(3, *this, 1.0);
    goals.emplace<WaterAvoidingRandomStrollGoal>(5, *this, 1.0);
    goals.emplace<LookAtPlayerGoal>(6, *this, 8.0f);
    goals.emplace<RandomLookAroundGoal>(6, *this);
    targets.emplace<HurtByTargetGoal>(1, *this);
    targets.emplace<NearestAttackableTargetGoal>(2, *this, true);
    bowGoal_ = std::make_unique<RangedBowAttackGoal>(*this, 1.0, 15.0f);
    meleeGoal_ = std::make_unique<MeleeAttackGoal>(*this, 1.2, false);
    goals.addShared(4, meleeGoal_.get());
}

Skeleton::~Skeleton() = default;

bool Skeleton::holdingBow() const {
    auto bow = [](const ItemStack& s) { return !s.empty() && s.item->id == "minecraft:bow"; };
    return bow(equipment[0]) || bow(equipment[1]);
}

void Skeleton::reassessWeaponGoal(Level& level) {
    goals.remove(level, bowGoal_.get());
    goals.remove(level, meleeGoal_.get());
    if (holdingBow()) {
        static_cast<RangedBowAttackGoal&>(*bowGoal_).minAttackInterval = level.difficulty == Difficulty::Hard ? 20 : 40;
        goals.addShared(4, bowGoal_.get());
    } else {
        goals.addShared(4, meleeGoal_.get());
    }
    reassess_ = false;
}

void Skeleton::finalizeSpawn(Level& level, SpawnReason reason, SpawnGroup& group) {
    Monster::finalizeSpawn(level, reason, group);
    populateDefaultEquipment(level);
    if (const ItemRegistry* items = level.itemRegistry())
        if (const Item* bow = items->find("bow")) setItemSlot(EquipmentSlot::MainHand, ItemStack{bow, 1});
    reassessWeaponGoal(level);
}

void Skeleton::loadExtra(const Json& in, const ItemRegistry& items) {
    Monster::loadExtra(in, items);
    reassess_ = true; // the weapon goal follows once there is a level
}

void Skeleton::customServerAiStep(Level& level) {
    if (reassess_) reassessWeaponGoal(level);
}

void Skeleton::mobAiStep(Level& level) {
    burnInDaylight(level);
    Monster::mobAiStep(level);
}

void Skeleton::performRangedAttack(Level& level, LivingEntity& target, float power) {
    const ItemRegistry* items = level.itemRegistry();
    auto arrow = std::make_unique<Arrow>();
    arrow->owner = id;
    arrow->leftOwner = false;
    arrow->pos = arrow->oldPos = glm::dvec3(pos.x, eyePos().y - 0.1, pos.z);
    if (items)
        if (const Item* a = items->find("arrow")) arrow->pickupItem = ItemStack{a, 1};
    // Vanilla setBaseDamageFromMob: 2 x power plus a little by difficulty.
    const double d = double(int(level.difficulty));
    arrow->baseDamage = double(power * 2.0f) + d * 0.11 + 0.57425 * (random.nextDouble() - random.nextDouble());
    const double dx = target.pos.x - pos.x, dz = target.pos.z - pos.z;
    const double dy = target.pos.y + double(target.height) / 3.0 - arrow->pos.y;
    const double horizontal = std::sqrt(dx * dx + dz * dz);
    arrow->shoot({dx, dy + horizontal * 0.2f, dz}, 1.6f, float(14 - int(level.difficulty) * 4), random);
    level.addEntity(std::move(arrow));
}

// Creeper

Creeper::Creeper(const EntityTypeInfo& type) : Monster(type) {
    goals.emplace<FloatGoal>(1, *this);
    goals.emplace<SwellGoal>(2, *this);
    goals.emplace<MeleeAttackGoal>(4, *this, 1.0, false);
    goals.emplace<WaterAvoidingRandomStrollGoal>(5, *this, 0.8);
    goals.emplace<LookAtPlayerGoal>(6, *this, 8.0f);
    goals.emplace<RandomLookAroundGoal>(6, *this);
    targets.emplace<NearestAttackableTargetGoal>(1, *this, true);
    targets.emplace<HurtByTargetGoal>(2, *this);
}

void Creeper::tick(Level& level) {
    if (alive()) {
        oldSwell = swell;
        if (ignited) swellDir = 1;
        swell = std::max(0, swell + swellDir);
        if (swell >= maxSwell) {
            swell = maxSwell;
            explode(level);
            return;
        }
    }
    Monster::tick(level);
}

void Creeper::explode(Level& level) {
    // Vanilla explodeCreeper: power 3 (6 when charged), blocks only with mob griefing, drops
    // decaying; the creeper is gone.
    health = 0.0f;
    deathHandled_ = true;
    removed = true;
    const float power = float(explosionRadius) * (powered ? 2.0f : 1.0f);
    sim::explode(level, this, pos, power, level.mobGriefing, true);
}

void Creeper::onLanded(double fallDistance) {
    swell = std::min(swell + int(float(fallDistance) * 1.5f), maxSwell - 5);
}

bool Creeper::interact(Level& level, Player& player, ItemStack& held) {
    // Vanilla creeper_igniters: flint and steel lights the fuse (and wears by one).
    if (!held.empty() && held.item->id == "minecraft:flint_and_steel") {
        ignited = true;
        if (!player.invulnerable() && held.hurt(1)) held = ItemStack{};
        return true;
    }
    return Monster::interact(level, player, held);
}

void Creeper::thunderHit(Level& level) {
    Monster::thunderHit(level);
    powered = true;
}

void Creeper::saveExtra(Json& out) const {
    Monster::saveExtra(out);
    out["powered"] = powered;
    out["ignited"] = ignited;
    out["fuse"] = maxSwell;
    out["explosion_radius"] = explosionRadius;
    out["swell"] = swell;
}

void Creeper::loadExtra(const Json& in, const ItemRegistry& items) {
    Monster::loadExtra(in, items);
    powered = in.value("powered", false);
    ignited = in.value("ignited", false);
    maxSwell = std::max(1, in.value("fuse", 30));
    explosionRadius = in.value("explosion_radius", 3);
    swell = oldSwell = in.value("swell", 0);
}

// Spider

Spider::Spider(const EntityTypeInfo& type) : Monster(type) {
    navigation.wallClimber = true;
    goals.emplace<FloatGoal>(1, *this);
    goals.emplace<LeapAtTargetGoal>(3, *this, 0.4f);
    goals.emplace<SpiderAttackGoal>(4, *this);
    goals.emplace<WaterAvoidingRandomStrollGoal>(5, *this, 0.8);
    goals.emplace<LookAtPlayerGoal>(6, *this, 8.0f);
    goals.emplace<RandomLookAroundGoal>(6, *this);
    targets.emplace<HurtByTargetGoal>(1, *this);
    // Vanilla SpiderTargetGoal: only hunts in the dark.
    targets.emplace<NearestAttackableTargetGoal>(2, *this, true, [this](Level& level) {
        return magicBrightness(level, BlockPos::containing(eyePos())) < 0.5f;
    });
}

void Spider::tick(Level& level) {
    Monster::tick(level);
    climbing = horizontalCollision;
}

} // namespace mc
