#include "entity/mob/Mob.h"

#include <cmath>

#include "entity/ExperienceOrb.h"
#include "entity/ItemEntity.h"
#include "entity/Physics.h"
#include "entity/Player.h"
#include "game/Combat.h"
#include "resources/ResourcePack.h"
#include "world/level/Level.h"
#include "world/level/Raycast.h"
#include "world/sim/Weathering.h"

namespace mc {
namespace {

constexpr float kDegToRad = 3.14159265358979f / 180.0f;
constexpr float kRadToDeg = 180.0f / 3.14159265358979f;

const char* const kArmorTiers[5] = {"leather", "golden", "chainmail", "iron", "diamond"};
const char* const kArmorPieces[4] = {"boots", "leggings", "chestplate", "helmet"}; // feet .. head

} // namespace

float specialMultiplier(const Level& level) {
    // Vanilla DifficultyInstance.calculateDifficulty (inhabited time 0) and getSpecialMultiplier.
    if (level.difficulty == Difficulty::Peaceful) return 0.0f;
    const bool hard = level.difficulty == Difficulty::Hard;
    float f = 0.75f;
    const float age = std::clamp((float(level.gameTime()) - 72000.0f) / 1440000.0f, 0.0f, 1.0f) * 0.25f;
    f += age;
    static constexpr float kMoon[8] = {1.0f, 0.75f, 0.5f, 0.25f, 0.0f, 0.25f, 0.5f, 0.75f};
    const float moon = kMoon[size_t((level.dayTime() / Level::kDayLength % 8 + 8) % 8)];
    float extra = std::clamp(moon * 0.25f, 0.0f, age);
    (void)hard; // the inhabited-time term (x1 on Hard, x0.75 otherwise) is 0
    if (level.difficulty == Difficulty::Easy) extra *= 0.5f;
    f += extra;
    const float effective = float(int(level.difficulty)) * f;
    return effective < 2.0f ? 0.0f : effective > 4.0f ? 1.0f : (effective - 2.0f) / 2.0f;
}

Mob::Mob(const EntityTypeInfo& type)
    : moveControl(*this), lookControl(*this), jumpControl(*this), bodyRotation(*this), navigation(*this),
      sensing(*this), type_(&type) {
    stepHeight = 0.6f;
    maxHealth = health = type.maxHealth;
    movementSpeed = type.movementSpeed;
    followRange = type.followRange;
    attackDamage = type.attackDamage;
    baseArmor = type.armor;
    refreshDimensions();
}

Mob::~Mob() = default;

void Mob::refreshDimensions() {
    width = type_->width * ageScale();
    height = type_->height * ageScale();
}

void Mob::setItemSlot(EquipmentSlot slot, ItemStack stack) {
    equipment[size_t(slot)] = std::move(stack);
    updateArmorValue();
}

float Mob::attackDamageWithWeapon() const {
    // The weapon's modifier: its damage less the player's base 1.
    const ItemStack& held = mainHand();
    return attackDamage + (held.empty() || !held.item->attackModifiers ? 0.0f : held.item->attackDamage - 1.0f);
}

LivingEntity* Mob::target(const Level& level) const {
    if (!targetId) return nullptr;
    auto* e = dynamic_cast<LivingEntity*>(level.entity(targetId));
    return e && !e->removed ? e : nullptr;
}

PathMob Mob::pathMob(const Level& level) const {
    PathMob m;
    m.pos = pos;
    m.width = width;
    m.height = height;
    m.stepHeight = stepHeight;
    m.onGround = onGround;
    m.inWater = inWater;
    m.malus = malus;
    m.canFloat = navigation.canFloat;
    m.maxFallDistance = maxFallDistance(level);
    return m;
}

int Mob::maxFallDistance(const Level& level) const {
    if (!target(level)) return 3;
    int i = int(health - maxHealth * 0.33f);
    i -= (3 - int(level.difficulty)) * 4;
    return std::max(i, 0) + 3;
}

bool Mob::onClimbable(const Level& level) const {
    static thread_local const BlockRegistry* cached = nullptr;
    static thread_local TagId climbable = 0;
    if (cached != &level.registry()) {
        cached = &level.registry();
        climbable = level.registry().tag("climbable").value_or(TagId(0));
    }
    return level.hasTag(BlockPos::containing(pos), climbable);
}

float Mob::brightness(const Level& level) const {
    const BlockPos eye = BlockPos::containing(eyePos());
    const float f = float(level.rawBrightness(eye, level.skyDarken())) / 15.0f;
    return f / (4.0f - 3.0f * f); // DimensionType.brightness with no ambient light
}

bool Mob::isSunBurnTick(const Level& level) {
    if (!level.isBrightOutside()) return false;
    const float f = brightness(level);
    const BlockPos eye = BlockPos::containing(eyePos());
    const bool wet = inWater || inRain;
    return f > 0.5f && random.nextFloat() * 30.0f < (f - 0.4f) * 2.0f && !wet && level.canSeeSky(eye);
}

void Mob::lookAt(const Entity& e, float maxYaw, float maxPitch) {
    const double dx = e.pos.x - pos.x, dz = e.pos.z - pos.z;
    const auto* living = dynamic_cast<const LivingEntity*>(&e);
    const double dy = (living ? living->eyePos().y : (e.box().min.y + e.box().max.y) / 2.0) - eyePos().y;
    const double horizontal = std::sqrt(dx * dx + dz * dz);
    const float wantYaw = float(std::atan2(dz, dx)) * kRadToDeg - 90.0f;
    const float wantPitch = -float(std::atan2(dy, horizontal)) * kRadToDeg;
    auto rotlerp = [](float a, float b, float max) { return a + std::clamp(wrapDegrees(b - a), -max, max); };
    pitch = rotlerp(pitch, wantPitch, maxPitch);
    yaw = rotlerp(yaw, wantYaw, maxYaw);
}

bool Mob::hasLineOfSight(const Level& level, const Entity& e) const {
    const glm::dvec3 from = eyePos();
    const auto* living = dynamic_cast<const LivingEntity*>(&e);
    const glm::dvec3 to(e.pos.x, living ? living->eyePos().y : e.pos.y + e.height * 0.85, e.pos.z);
    const glm::dvec3 d = to - from;
    const double length = glm::length(d);
    if (length > 128.0) return false;
    if (length < 1e-7) return true;
    return !raycastBlocks(level, from, d / length, length, RayFluid::None, RayShape::Collider);
}

bool Mob::withinMeleeRange(const Entity& target) const {
    constexpr double kReach = 0.8282828; // sqrt(2.04) - 0.6
    return box().inflated(kReach, 0.0, kReach).intersects(target.box());
}

bool Mob::doHurtTarget(Level& level, LivingEntity& target) {
    const float damage = attackDamageWithWeapon();
    const bool hit = target.hurtServer(level, DamageSource::mobAttack(*this), damage);
    if (hit) {
        // The weapon wears (swords 1, tools 2).
        ItemStack& held = mainHand();
        if (!held.empty() && held.item->maxDamage > 0) {
            if (held.hurt(held.item->tool == ToolKind::Sword ? 1 : 2)) held = ItemStack{};
        }
    }
    return hit;
}

void Mob::jumpFromGround(const Level& level) {
    const BlockRegistry& reg = level.registry();
    float factor = reg.blockOf(level.getBlock(BlockPos::containing(pos))).jumpFactor;
    if (factor == 1.0f) factor = reg.blockOf(level.getBlock(blockBelow())).jumpFactor;
    velocity.y = std::max(0.42 * double(factor), velocity.y);
}

void Mob::travel(Level& level, glm::vec3 input) {
    auto moveRelative = [&](float amount, const glm::vec3& in) {
        const float lenSq = glm::dot(in, in);
        if (lenSq < 1.0e-7f) return;
        const glm::vec3 v = (lenSq > 1.0f ? glm::normalize(in) : in) * amount;
        const float s = std::sin(yaw * kDegToRad), c = std::cos(yaw * kDegToRad);
        velocity += glm::dvec3(v.x * c - v.z * s, v.y, v.z * c + v.x * s);
    };
    auto isFree = [&](const glm::dvec3& d) {
        const AABB b = box().moved(d);
        if (!noCollision(level, b)) return false;
        for (int y = int(std::floor(b.min.y)); y <= int(std::floor(b.max.y)); ++y)
            for (int z = int(std::floor(b.min.z)); z <= int(std::floor(b.max.z)); ++z)
                for (int x = int(std::floor(b.min.x)); x <= int(std::floor(b.max.x)); ++x)
                    if (!level.fluid({x, y, z}).empty()) return false;
        return true;
    };
    const double g = gravity();
    auto fluidFalling = [&](bool falling, double vy) {
        if (g == 0.0) return vy;
        if (falling && std::abs(vy - 0.005) >= 0.003 && std::abs(vy - g / 16.0) < 0.003) return -0.003;
        return vy - g / 16.0;
    };
    const bool climbable = onClimbable(level);
    if (inWater || inLava) {
        // Vanilla travelInFluid.
        const bool falling = velocity.y <= 0.0;
        const double startY = pos.y;
        if (inWater) {
            moveRelative(0.02f, input);
            move(level, velocity);
            glm::dvec3 v = velocity;
            if (horizontalCollision && climbable) v.y = 0.2;
            v *= glm::dvec3(0.8, 0.8, 0.8);
            v.y = fluidFalling(falling, v.y);
            velocity = v;
        } else {
            moveRelative(0.02f, input);
            move(level, velocity);
            if (lavaHeight <= (eyeHeight() < 0.4f ? 0.0 : 0.4)) {
                velocity *= glm::dvec3(0.5, 0.8, 0.5);
                velocity.y = fluidFalling(falling, velocity.y);
            } else {
                velocity *= 0.5;
            }
            if (g != 0.0) velocity.y -= g / 4.0;
        }
        if (horizontalCollision && isFree({velocity.x, velocity.y + 0.6 - pos.y + startY, velocity.z})) velocity.y = 0.3;
        return;
    }
    // Vanilla travelInAir.
    const float blockFriction = level.registry().blockOf(level.getBlock(blockBelow())).friction;
    const float f = onGround ? blockFriction : 1.0f;
    const float friction = f * 0.91f;
    moveRelative(onGround ? speed * (0.21600002f / (f * f * f)) : 0.02f, input);
    if (climbable) {
        fallDistance = 0.0;
        velocity.x = std::clamp(velocity.x, -0.15, 0.15);
        velocity.z = std::clamp(velocity.z, -0.15, 0.15);
        velocity.y = std::max(velocity.y, -0.15);
    }
    move(level, velocity);
    glm::dvec3 v = velocity;
    if ((horizontalCollision || jumping) && climbable) v.y = 0.2;
    v.y -= g;
    velocity = glm::dvec3(v.x * friction, v.y * 0.98, v.z * friction);
}

void Mob::serverAiStep(Level& level) {
    ++noActionTime;
    sensing.tick();
    if ((tickCount + int(id)) % 2 != 0 && tickCount > 1) {
        targets.tickRunning(level, false);
        goals.tickRunning(level, false);
    } else {
        targets.tick(level);
        goals.tick(level);
    }
    navigation.tick(level);
    customServerAiStep(level);
    moveControl.tick(level);
    lookControl.tick();
    jumpControl.tick();
}

void Mob::checkDespawn(Level& level) {
    if (level.difficulty == Difficulty::Peaceful && despawnsInPeaceful()) {
        removed = true;
        return;
    }
    if (persistenceRequired) {
        noActionTime = 0;
        return;
    }
    const Player* p = level.player();
    if (!p) return;
    const glm::dvec3 d = p->pos - pos;
    const double distanceSq = glm::dot(d, d);
    if (distanceSq > 128.0 * 128.0 && removeWhenFarAway(distanceSq)) {
        removed = true;
        return;
    }
    if (noActionTime > 600 && random.nextInt(800) == 0 && distanceSq > 32.0 * 32.0 && removeWhenFarAway(distanceSq))
        removed = true;
    else if (distanceSq < 32.0 * 32.0)
        noActionTime = 0;
}

void Mob::tick(Level& level) {
    checkDespawn(level);
    if (removed) return;
    oldPos = pos;
    oldYaw = yaw;
    oldPitch = pitch;
    beginLivingTick();
    updateFluidState(level);
    inRain = level.isRainingAt(BlockPos::containing(pos)) ||
             level.isRainingAt(BlockPos::containing({pos.x, pos.y + height, pos.z}));
    environmentTick(level);
    if (dead() && !deathHandled_) die(level);
    if (dead() && deathTime >= 20) {
        // Vanilla tickDeath: gone in a puff of smoke.
        removed = true;
        level.levelEvent(LevelEventType::EntityPoof, BlockPos::containing(pos), 0,
                         int(width * 100.0f) | (int(height * 100.0f) << 16), pos);
        return;
    }
    // Landing on farmland tramples it when mobs may grief and it is big enough.
    if (auto landed = std::exchange(landedOnFarmland, std::nullopt);
        landed && level.mobGriefing && width * width * height > 0.512f)
        sim::trampleFarmland(level, landed->first, landed->second);

    // Vanilla LivingEntity.aiStep.
    if (noJumpDelay_ > 0) --noJumpDelay_;
    for (int a = 0; a < 3; ++a)
        if (std::abs(velocity[a]) < 0.003) velocity[a] = 0.0;
    updateSwingTime();
    if (usingItem()) --useItemRemaining;
    if (dead() || noAi) {
        jumping = false;
        xxa = zza = 0.0f;
    } else {
        serverAiStep(level);
    }
    if (jumping) {
        const double depth = inLava ? lavaHeight : waterHeight;
        const bool swimming = inWater && depth > 0.0;
        const double threshold = eyeHeight() < 0.4f ? 0.0 : 0.4;
        if (!swimming || (onGround && !(depth > threshold))) {
            if (!inLava || (onGround && !(depth > threshold))) {
                if ((onGround || (swimming && depth <= threshold)) && noJumpDelay_ == 0) {
                    jumpFromGround(level);
                    noJumpDelay_ = 10;
                }
            } else {
                velocity.y += 0.04; // jumpInLiquid(lava)
            }
        } else {
            velocity.y += 0.04; // jumpInLiquid(water)
        }
    } else {
        noJumpDelay_ = 0;
    }
    xxa *= 0.98f;
    zza *= 0.98f;
    travel(level, glm::vec3(xxa, yya, zza));
    // Vanilla pushEntities: shoves what it overlaps, and is shoved back.
    if (alive()) {
        for (Entity* e : level.entitiesIn(box(), this))
            if (e->isPushable()) pushApart(*this, *e);
        if (Player* p = level.player(); p && p->isPushable() && p->box().intersects(box())) pushApart(*this, *p);
    }
    // (NoAI stops the goals, not the rest: the undead still burn, chickens still lay.)
    if (alive()) mobAiStep(level);
    // Vanilla LivingEntity.tick after aiStep: the body turns (BodyRotationControl), the limbs swing.
    bodyRotation.tick();
    updateWalkAnimation();
}

bool Mob::hurtServer(Level& level, const DamageSource& source, float amount) {
    if (alive()) noActionTime = 0;
    const bool hurt = LivingEntity::hurt(source, amount);
    if (dead() && !deathHandled_) die(level);
    return hurt;
}

DeathContext Mob::deathContext() const {
    DeathContext d;
    d.killedByPlayer = lastHurtByPlayerTime > 0;
    d.onFire = onFire();
    d.killerType = lastAttackerType;
    return d;
}

int Mob::experienceReward() { return 0; }

void Mob::die(Level& level) {
    deathHandled_ = true;
    goals.stopAll(level);
    targets.stopAll(level);
    navigation.stop();
    stopUsingItem();
    if (level.doMobLoot) {
        if (const EntityTypes* types = level.entityTypes())
            for (const ItemStack& stack : types->loot(*type_, deathContext(), random)) spawnAtLocation(level, stack);
        dropCustomDeathLoot(level);
        if (lastHurtByPlayerTime > 0) {
            const int xp = experienceReward();
            if (xp > 0) ExperienceOrb::award(level, pos, xp);
        }
    }
}

void Mob::dropCustomDeathLoot(Level& level) {
    // Vanilla Mob.dropCustomDeathLoot: worn and held items, by their drop chances, after a
    // player's hit (those above 1 always); the chance ones come out worn.
    const bool recentlyHit = lastHurtByPlayerTime > 0;
    for (size_t slot = 0; slot < equipment.size(); ++slot) {
        ItemStack stack = equipment[slot];
        const float chance = dropChances[slot];
        if (stack.empty() || !(recentlyHit || chance > 1.0f) || random.nextFloat() >= chance) continue;
        if (chance <= 1.0f && stack.item->maxDamage > 0) {
            const int max = stack.item->maxDamage;
            stack.damage = std::max(0, max - random.nextInt(1 + random.nextInt(std::max(max - 3, 1))));
        }
        spawnAtLocation(level, stack);
        equipment[slot] = ItemStack{};
    }
}

void Mob::spawnAtLocation(Level& level, const ItemStack& stack, double yOffset) {
    if (stack.empty()) return;
    // Vanilla spawnAtLocation: at the feet (plus the offset), with ItemEntity's default throw.
    ItemEntity& e = ItemEntity::spawn(level, pos + glm::dvec3(0.0, yOffset, 0.0), stack);
    e.pickupDelay = 10;
}

void Mob::populateDefaultEquipment(Level& level) {
    // Vanilla populateDefaultEquipmentSlots: by the local difficulty, a random armor tier for
    // the head down to the feet, each further piece only by chance.
    if (random.nextFloat() >= 0.15f * specialMultiplier(level)) return;
    int tier = random.nextInt(2);
    const float stopChance = level.difficulty == Difficulty::Hard ? 0.1f : 0.25f;
    for (int k = 0; k < 3; ++k)
        if (random.nextFloat() < 0.095f) ++tier;
    const ItemRegistry* items = level.itemRegistry();
    if (!items) return;
    bool first = true;
    for (int slot = 3; slot >= 0; --slot) { // head, chest, legs, feet
        if (!first && random.nextFloat() < stopChance) break;
        first = false;
        ItemStack& piece = armorItems()[size_t(slot)];
        if (!piece.empty()) continue;
        const std::string id = std::string(kArmorTiers[std::min(tier, 4)]) + "_" + kArmorPieces[slot];
        if (const Item* it = items->find(id)) piece = ItemStack{it, 1};
    }
    updateArmorValue();
}

void Mob::finalizeSpawn(Level& level, SpawnReason reason, SpawnGroup& group) {
    (void)reason, (void)group;
    // Vanilla Mob.finalizeSpawn: a random follow-range bonus (triangle around 0, +-11.5%).
    followRange *= float(1.0 + (random.nextDouble() - random.nextDouble()) * 0.11485000000000001);
    (void)level;
}

bool Mob::checkSpawnRules(const Level& level, SpawnReason reason) const {
    (void)reason;
    return walkTargetValue(level, BlockPos::containing(pos)) >= 0.0f;
}

bool Mob::interact(Level& level, Player& player, ItemStack& held) {
    // Vanilla checkAndHandleImportantInteractions: a spawn egg of its own kind makes a baby.
    if (!held.empty() && spawnEggType(*held.item)) {
        if (!spawnBabyFromEgg(level, *this, *held.item)) return false;
        if (!player.invulnerable()) held.shrink(1);
        return true;
    }
    return false;
}

void Mob::thunderHit(Level& level) {
    // Vanilla Entity.thunderHit: set alight for 8 seconds, and 5 damage.
    igniteForTicks(160);
    hurtServer(level, DamageType::LightningBolt, 5.0f);
}

void Mob::saveExtra(Json& out) const {
    out["health"] = health;
    out["fire"] = remainingFireTicks;
    out["air"] = airSupply;
    out["hurt_time"] = hurtTime;
    out["death_time"] = deathTime;
    out["fall_distance"] = fallDistance;
    out["persistent"] = persistenceRequired;
    if (noAi) out["no_ai"] = true;
    out["head_yaw"] = headYaw;
    out["body_yaw"] = bodyYaw;
    out["follow_range"] = followRange;
    Json eq = Json::array();
    for (const ItemStack& s : equipment) eq.push_back(s.empty() ? Json() : stackToJson(s));
    out["equipment"] = std::move(eq);
    Json chances = Json::array();
    for (float c : dropChances) chances.push_back(c);
    out["drop_chances"] = std::move(chances);
}

void Mob::loadExtra(const Json& in, const ItemRegistry& items) {
    health = in.value("health", maxHealth);
    remainingFireTicks = in.value("fire", 0);
    airSupply = in.value("air", maxAirSupply);
    hurtTime = in.value("hurt_time", 0);
    deathTime = in.value("death_time", 0);
    fallDistance = in.value("fall_distance", 0.0);
    persistenceRequired = in.value("persistent", false);
    noAi = in.value("no_ai", false);
    headYaw = oldHeadYaw = in.value("head_yaw", yaw);
    bodyYaw = oldBodyYaw = in.value("body_yaw", yaw);
    followRange = in.value("follow_range", followRange);
    if (const Json eq = in.value("equipment", Json::array()); eq.is_array())
        for (size_t i = 0; i < eq.size() && i < equipment.size(); ++i)
            equipment[i] = eq[i].is_null() ? ItemStack{} : stackFromJson(eq[i], items);
    if (const Json c = in.value("drop_chances", Json::array()); c.is_array())
        for (size_t i = 0; i < c.size() && i < dropChances.size(); ++i) dropChances[i] = c[i].get<float>();
    if (dead()) deathHandled_ = true; // saved mid-fall: its loot already dropped
    updateArmorValue();
}

} // namespace mc
