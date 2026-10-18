#include "entity/ai/Goals.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "entity/Player.h"
#include "entity/mob/Mob.h"
#include "entity/path/PathTypes.h"
#include "game/Item.h"
#include "world/level/Level.h"

namespace mc {
namespace {

constexpr double kTwoPi = 6.283185307179586;

// Vanilla getVisibilityPercent: sneaking players are harder to notice, and so is one
// wearing the looking mob's own head.
double visibility(const LivingEntity& target, const Mob& looker) {
    double v = 1.0;
    if (const auto* p = dynamic_cast<const Player*>(&target)) {
        if (p->crouching) v *= 0.8;
        const ItemStack& head = p->inventory.armor[3];
        if (!head.empty()) {
            const std::string_view type = looker.type(), item = head.item->id;
            if ((type == "minecraft:skeleton" && item == "minecraft:skeleton_skull") ||
                (type == "minecraft:zombie" && item == "minecraft:zombie_head") ||
                (type == "minecraft:creeper" && item == "minecraft:creeper_head"))
                v *= 0.5;
        }
    }
    return v;
}

double distanceSq(const Entity& a, const Entity& b) {
    const glm::dvec3 d = a.pos - b.pos;
    return glm::dot(d, d);
}

// Vanilla Mob.canAttack: the player on Peaceful, creative or spectating is no target.
bool canAttack(const Level& level, const LivingEntity& target) {
    if (target.dead() || target.removed) return false;
    if (const auto* p = dynamic_cast<const Player*>(&target))
        return level.difficulty != Difficulty::Peaceful && !p->invulnerable();
    return true;
}

std::optional<glm::dvec3> bestOf(Mob& mob, Level& level, const std::function<std::optional<BlockPos>()>& candidate) {
    double best = -std::numeric_limits<double>::infinity();
    std::optional<BlockPos> chosen;
    for (int i = 0; i < 10; ++i) {
        const std::optional<BlockPos> p = candidate();
        if (!p) continue;
        const double value = mob.walkTargetValue(level, *p);
        if (value > best) {
            best = value;
            chosen = p;
        }
    }
    if (!chosen) return std::nullopt;
    return glm::dvec3(chosen->x + 0.5, chosen->y, chosen->z + 0.5);
}

BlockPos randomDirection(Xoroshiro& r, int h, int v) {
    const int x = r.nextInt(2 * h + 1) - h;
    const int y = r.nextInt(2 * v + 1) - v;
    const int z = r.nextInt(2 * h + 1) - h;
    return {x, y, z};
}

BlockPos towardDirection(const Mob& mob, const BlockPos& d) {
    return BlockPos::containing({double(d.x) + mob.pos.x, double(d.y) + mob.pos.y, double(d.z) + mob.pos.z});
}

bool outsideLimits(const BlockPos& p) { return p.y < kWorldMinY || p.y >= kWorldMaxY; }

bool hasMalus(const Level& level, const Mob& mob, const BlockPos& p) {
    return mob.pathfindingMalus(pathTypeAt(level, p)) != 0.0f;
}

} // namespace

bool causesPanic(DamageType t) {
    switch (t) {
    case DamageType::Cactus:
    case DamageType::HotFloor:
    case DamageType::InFire:
    case DamageType::Lava:
    case DamageType::LightningBolt:
    case DamageType::OnFire:
    case DamageType::Arrow:
    case DamageType::Explosion:
    case DamageType::MobAttack:
    case DamageType::PlayerAttack:
    case DamageType::PlayerExplosion:
    case DamageType::Campfire: return true;
    default: return false;
    }
}

bool canTarget(Level& level, Mob& mob, const LivingEntity& target, double range, bool combat, bool ignoreSight) {
    if (&target == &mob || target.removed || target.dead()) return false;
    if (const auto* p = dynamic_cast<const Player*>(&target); p && p->mode() == GameMode::Spectator) return false;
    if (combat && !canAttack(level, target)) return false;
    if (range > 0.0) {
        const double r = std::max(range * visibility(target, mob), 2.0);
        if (distanceSq(mob, target) > r * r) return false;
    }
    return ignoreSight || mob.sensing.hasLineOfSight(level, target);
}

std::optional<glm::dvec3> defaultRandomPos(Level& level, Mob& mob, int radius, int vertical) {
    return bestOf(mob, level, [&]() -> std::optional<BlockPos> {
        const BlockPos p = towardDirection(mob, randomDirection(mob.random, radius, vertical));
        if (outsideLimits(p) || !mob.navigation.isStableDestination(level, p) || hasMalus(level, mob, p))
            return std::nullopt;
        return p;
    });
}

std::optional<glm::dvec3> landRandomPos(Level& level, Mob& mob, int radius, int vertical) {
    const BlockRegistry& reg = level.registry();
    return bestOf(mob, level, [&]() -> std::optional<BlockPos> {
        BlockPos p = towardDirection(mob, randomDirection(mob.random, radius, vertical));
        if (outsideLimits(p) || !mob.navigation.isStableDestination(level, p)) return std::nullopt;
        // Up out of anything solid, then not into water or a costly spot.
        while (p.y < kWorldMaxY && pathBlockInfo(reg, level.getBlock(p)).solid) p.y += 1;
        if (level.fluid(p).kind == FluidKind::Water || hasMalus(level, mob, p)) return std::nullopt;
        return p;
    });
}

// FloatGoal

FloatGoal::FloatGoal(Mob& mob) : mob_(mob) {
    flags = kGoalJump;
    mob.navigation.canFloat = true;
}

bool FloatGoal::canUse(Level& level) {
    (void)level;
    const double threshold = mob_.eyeHeight() < 0.4f ? 0.0 : 0.4;
    return (mob_.inWater && mob_.waterHeight > threshold) || mob_.inLava;
}

void FloatGoal::tick(Level& level) {
    (void)level;
    if (mob_.random.nextFloat() < 0.8f) mob_.jumpControl.jump();
}

// PanicGoal

bool PanicGoal::shouldPanic() const {
    return (mob_.tickCount - mob_.lastHurtTick <= 40 && causesPanic(mob_.lastHurtType)) || mob_.onFire();
}

bool PanicGoal::canUse(Level& level) {
    if (!shouldPanic()) return false;
    if (mob_.onFire()) {
        // Vanilla lookForWater: the nearest water within 5 across and 1 up or down.
        const BlockPos feet = BlockPos::containing(mob_.pos);
        if (level.registry().collisionShape(level.getBlock(feet)).empty()) {
            std::optional<BlockPos> water;
            int bestDistance = std::numeric_limits<int>::max();
            for (int dy = -1; dy <= 1; ++dy)
                for (int dz = -5; dz <= 5; ++dz)
                    for (int dx = -5; dx <= 5; ++dx) {
                        const int d = std::abs(dx) + std::abs(dy) + std::abs(dz);
                        if (d >= bestDistance) continue;
                        const BlockPos p = feet.offset(dx, dy, dz);
                        if (level.fluid(p).kind == FluidKind::Water) {
                            water = p;
                            bestDistance = d;
                        }
                    }
            if (water) {
                wanted_ = glm::dvec3(water->x, water->y, water->z);
                return true;
            }
        }
    }
    const auto p = defaultRandomPos(level, mob_, 5, 4);
    if (!p) return false;
    wanted_ = *p;
    return true;
}

bool PanicGoal::canContinueToUse(Level& level) {
    (void)level;
    return !mob_.navigation.isDone();
}

void PanicGoal::start(Level& level) {
    mob_.navigation.moveTo(level, wanted_, speed_);
    mob_.panicking = true;
}

void PanicGoal::stop(Level& level) {
    (void)level;
    mob_.panicking = false;
}

// RandomStrollGoal

bool RandomStrollGoal::canUse(Level& level) {
    if (!forceTrigger_) {
        if (checkNoActionTime_ && mob_.noActionTime >= 100) return false;
        if (mob_.random.nextInt(reducedTickDelay(interval_)) != 0) return false;
    }
    const auto p = position(level);
    if (!p) return false;
    wanted_ = *p;
    forceTrigger_ = false;
    return true;
}

bool RandomStrollGoal::canContinueToUse(Level& level) {
    (void)level;
    return !mob_.navigation.isDone();
}

void RandomStrollGoal::start(Level& level) { mob_.navigation.moveTo(level, wanted_, speed_); }

void RandomStrollGoal::stop(Level& level) {
    (void)level;
    mob_.navigation.stop();
}

std::optional<glm::dvec3> RandomStrollGoal::position(Level& level) { return defaultRandomPos(level, mob_, 10, 7); }

std::optional<glm::dvec3> WaterAvoidingRandomStrollGoal::position(Level& level) {
    if (mob_.inWater) {
        if (auto p = landRandomPos(level, mob_, 15, 7)) return p;
        return RandomStrollGoal::position(level);
    }
    return mob_.random.nextFloat() >= probability_ ? landRandomPos(level, mob_, 10, 7) : RandomStrollGoal::position(level);
}

// LookAtPlayerGoal

bool LookAtPlayerGoal::canUse(Level& level) {
    if (mob_.random.nextFloat() >= probability_) return false;
    lookAt_ = 0;
    if (Player* p = level.player(); p && canTarget(level, mob_, *p, distance_, false, false)) lookAt_ = p->id;
    return lookAt_ != 0;
}

bool LookAtPlayerGoal::canContinueToUse(Level& level) {
    auto* e = dynamic_cast<LivingEntity*>(level.entity(lookAt_));
    if (!e || !e->alive()) return false;
    if (distanceSq(mob_, *e) > double(distance_ * distance_)) return false;
    return lookTime_ > 0;
}

void LookAtPlayerGoal::start(Level& level) {
    (void)level;
    lookTime_ = adjustedTickDelay(40 + mob_.random.nextInt(40));
}

void LookAtPlayerGoal::stop(Level& level) {
    (void)level;
    lookAt_ = 0;
}

void LookAtPlayerGoal::tick(Level& level) {
    auto* e = dynamic_cast<LivingEntity*>(level.entity(lookAt_));
    if (!e || !e->alive()) return;
    mob_.lookControl.setLookAt(glm::dvec3(e->pos.x, e->eyePos().y, e->pos.z));
    --lookTime_;
}

// RandomLookAroundGoal

bool RandomLookAroundGoal::canUse(Level& level) {
    (void)level;
    return mob_.random.nextFloat() < 0.02f;
}

void RandomLookAroundGoal::start(Level& level) {
    (void)level;
    const double angle = kTwoPi * mob_.random.nextDouble();
    relX_ = std::cos(angle);
    relZ_ = std::sin(angle);
    lookTime_ = 20 + mob_.random.nextInt(20);
}

void RandomLookAroundGoal::tick(Level& level) {
    (void)level;
    --lookTime_;
    mob_.lookControl.setLookAt(glm::dvec3(mob_.pos.x + relX_, mob_.eyePos().y, mob_.pos.z + relZ_));
}

// TemptGoal

bool TemptGoal::tempting(const Level& level, const Player& p) const {
    const ItemRegistry* items = level.itemRegistry();
    auto holds = [&](const ItemStack& s) { return items && !s.empty() && items->inTag(*s.item, tag_); };
    return holds(p.inventory.selectedStack()) || holds(p.inventory.offhand);
}

bool TemptGoal::canUse(Level& level) {
    if (calmDown_ > 0) {
        --calmDown_;
        return false;
    }
    Player* p = level.player();
    return p && canTarget(level, mob_, *p, 10.0, false, true) && tempting(level, *p);
}

void TemptGoal::stop(Level& level) {
    (void)level;
    mob_.navigation.stop();
    calmDown_ = reducedTickDelay(100);
    running_ = false;
}

void TemptGoal::tick(Level& level) {
    Player* p = level.player();
    if (!p) return;
    mob_.lookControl.setLookAt(*p, mob_.maxHeadYRot() + 20.0f, mob_.maxHeadXRot());
    if (distanceSq(mob_, *p) < 6.25) mob_.navigation.stop();
    else mob_.navigation.moveTo(level, *p, speed_);
}

// MeleeAttackGoal

bool MeleeAttackGoal::canUse(Level& level) {
    const int64_t now = level.gameTime();
    if (now - lastCanUseCheck_ < 20) return false;
    lastCanUseCheck_ = now;
    LivingEntity* target = mob_.target(level);
    if (!target || !target->alive()) return false;
    path_ = mob_.navigation.createPath(level, *target, 0);
    return path_.has_value() || mob_.withinMeleeRange(*target);
}

bool MeleeAttackGoal::canContinueToUse(Level& level) {
    LivingEntity* target = mob_.target(level);
    if (!target || !target->alive()) return false;
    if (!followUnseen_) return !mob_.navigation.isDone();
    if (const auto* p = dynamic_cast<const Player*>(target)) return !p->invulnerable();
    return true;
}

void MeleeAttackGoal::start(Level& level) {
    mob_.navigation.moveTo(level, path_, speed_);
    mob_.aggressive = true;
    ticksUntilNextPathRecalculation_ = 0;
    ticksUntilNextAttack_ = 0;
}

void MeleeAttackGoal::stop(Level& level) {
    if (LivingEntity* target = mob_.target(level))
        if (const auto* p = dynamic_cast<const Player*>(target); p && p->invulnerable()) mob_.setTarget(nullptr);
    mob_.aggressive = false;
    mob_.navigation.stop();
}

void MeleeAttackGoal::tick(Level& level) {
    LivingEntity* target = mob_.target(level);
    if (!target) return;
    mob_.lookControl.setLookAt(*target, 30.0f, 30.0f);
    ticksUntilNextPathRecalculation_ = std::max(ticksUntilNextPathRecalculation_ - 1, 0);
    const bool sees = followUnseen_ || mob_.sensing.hasLineOfSight(level, *target);
    const glm::dvec3 moved = target->pos - pathedTarget_;
    if (sees && ticksUntilNextPathRecalculation_ <= 0 &&
        (pathedTarget_ == glm::dvec3(0.0) || glm::dot(moved, moved) >= 1.0 || mob_.random.nextFloat() < 0.05f)) {
        pathedTarget_ = target->pos;
        ticksUntilNextPathRecalculation_ = 4 + mob_.random.nextInt(7);
        const double d = distanceSq(mob_, *target);
        if (d > 1024.0) ticksUntilNextPathRecalculation_ += 10;
        else if (d > 256.0) ticksUntilNextPathRecalculation_ += 5;
        if (!mob_.navigation.moveTo(level, *target, speed_)) ticksUntilNextPathRecalculation_ += 15;
        ticksUntilNextPathRecalculation_ = adjustedTickDelay(ticksUntilNextPathRecalculation_);
    }
    ticksUntilNextAttack_ = std::max(ticksUntilNextAttack_ - 1, 0);
    checkAndPerformAttack(level, *target);
}

void MeleeAttackGoal::checkAndPerformAttack(Level& level, LivingEntity& target) {
    if (ticksUntilNextAttack_ > 0 || !mob_.withinMeleeRange(target) || !mob_.sensing.hasLineOfSight(level, target))
        return;
    ticksUntilNextAttack_ = adjustedTickDelay(20);
    mob_.swing();
    mob_.doHurtTarget(level, target);
}

// HurtByTargetGoal

bool HurtByTargetGoal::canUse(Level& level) {
    if (mob_.lastHurtByMobTime == timestamp_) return false;
    auto* attacker = dynamic_cast<LivingEntity*>(level.entity(mob_.recentAttacker()));
    return attacker && canTarget(level, mob_, *attacker, -1.0, true, true);
}

void HurtByTargetGoal::start(Level& level) {
    mob_.targetId = mob_.recentAttacker();
    timestamp_ = mob_.lastHurtByMobTime;
    unseenTicks_ = 0;
    (void)level;
}

bool HurtByTargetGoal::canContinueToUse(Level& level) {
    LivingEntity* target = mob_.target(level);
    if (!target || !canAttack(level, *target)) return false;
    if (distanceSq(mob_, *target) > double(mob_.followRange * mob_.followRange)) return false;
    if (mob_.sensing.hasLineOfSight(level, *target)) unseenTicks_ = 0;
    else if (++unseenTicks_ > reducedTickDelay(300)) return false;
    return true;
}

void HurtByTargetGoal::stop(Level& level) {
    (void)level;
    mob_.setTarget(nullptr);
}

// NearestAttackableTargetGoal

bool NearestAttackableTargetGoal::canUse(Level& level) {
    if (mob_.random.nextInt(reducedTickDelay(10)) != 0) return false;
    if (condition_ && !condition_(level)) return false;
    Player* p = level.player();
    found_ = p && canTarget(level, mob_, *p, mob_.followRange, true, false) ? p->id : 0;
    return found_ != 0;
}

bool NearestAttackableTargetGoal::canContinueToUse(Level& level) {
    LivingEntity* target = mob_.target(level);
    if (!target) target = dynamic_cast<LivingEntity*>(level.entity(found_));
    if (!target || !canAttack(level, *target)) return false;
    if (distanceSq(mob_, *target) > double(mob_.followRange * mob_.followRange)) return false;
    if (mustSee_) {
        if (mob_.sensing.hasLineOfSight(level, *target)) unseenTicks_ = 0;
        else if (++unseenTicks_ > reducedTickDelay(60)) return false;
    }
    mob_.setTarget(target);
    return true;
}

void NearestAttackableTargetGoal::start(Level& level) {
    (void)level;
    mob_.targetId = found_;
    unseenTicks_ = 0;
}

void NearestAttackableTargetGoal::stop(Level& level) {
    (void)level;
    mob_.setTarget(nullptr);
    found_ = 0;
}

// LeapAtTargetGoal

bool LeapAtTargetGoal::canUse(Level& level) {
    LivingEntity* t = mob_.target(level);
    if (!t) return false;
    target_ = t->id;
    const double d = distanceSq(mob_, *t);
    if (d < 4.0 || d > 16.0 || !mob_.onGround) return false;
    return mob_.random.nextInt(reducedTickDelay(5)) == 0;
}

bool LeapAtTargetGoal::canContinueToUse(Level& level) {
    (void)level;
    return !mob_.onGround;
}

void LeapAtTargetGoal::start(Level& level) {
    Entity* t = level.entity(target_);
    if (!t) return;
    glm::dvec3 leap(t->pos.x - mob_.pos.x, 0.0, t->pos.z - mob_.pos.z);
    if (glm::dot(leap, leap) > 1.0e-7) leap = glm::normalize(leap) * 0.4 + mob_.velocity * 0.2;
    mob_.velocity = {leap.x, double(yd_), leap.z};
}

// RestrictSunGoal, FleeSunGoal

bool RestrictSunGoal::canUse(Level& level) { return level.isBrightOutside() && mob_.equipment[5].empty(); }

void RestrictSunGoal::start(Level& level) {
    (void)level;
    mob_.navigation.avoidSun = true;
}

void RestrictSunGoal::stop(Level& level) {
    (void)level;
    mob_.navigation.avoidSun = false;
}

bool FleeSunGoal::canUse(Level& level) {
    if (mob_.target(level) || !level.isBrightOutside() || !mob_.onFire()) return false;
    if (!level.canSeeSky(BlockPos::containing(mob_.pos)) || !mob_.equipment[5].empty()) return false;
    // Vanilla getHidePos: 10 tries within 10 across and 3 up or down for shade.
    const BlockPos feet = BlockPos::containing(mob_.pos);
    for (int i = 0; i < 10; ++i) {
        const int dx = mob_.random.nextInt(20) - 10, dy = mob_.random.nextInt(6) - 3, dz = mob_.random.nextInt(20) - 10;
        const BlockPos p = feet.offset(dx, dy, dz);
        if (!level.canSeeSky(p) && mob_.walkTargetValue(level, p) < 0.0f) {
            wanted_ = glm::dvec3(p.x + 0.5, p.y, p.z + 0.5);
            return true;
        }
    }
    return false;
}

bool FleeSunGoal::canContinueToUse(Level& level) {
    (void)level;
    return !mob_.navigation.isDone();
}

void FleeSunGoal::start(Level& level) { mob_.navigation.moveTo(level, wanted_, speed_); }

} // namespace mc
