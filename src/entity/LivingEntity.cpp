#include "entity/LivingEntity.h"

#include <cmath>

#include "entity/ItemEntity.h"
#include "game/Item.h"
#include "resources/ResourcePack.h"
#include "world/chunk/Chunk.h"

namespace mc {

float wrapDegrees(float degrees) {
    float d = std::fmod(degrees, 360.0f);
    if (d >= 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    return d;
}

float rotLerp(float t, float a, float b) { return a + t * wrapDegrees(b - a); }

std::string entityNameKey(const Entity& e) {
    if (e.isPlayer()) return "entity.minecraft.player";
    std::string key = "entity." + std::string(e.type());
    for (char& c : key)
        if (c == ':') c = '.';
    return key;
}

void LivingEntity::swing(bool offhand) {
    if (!swinging_ || swingTime_ >= kSwingDuration / 2 || swingTime_ < 0) {
        swingTime_ = -1;
        swinging_ = true;
        swingOffhand_ = offhand;
    }
}

float LivingEntity::attackAnim(float partialTick) const {
    float d = attackAnim_ - oldAttackAnim_;
    if (d < 0.0f) d += 1.0f; // the swing ended and wrapped to 0
    return oldAttackAnim_ + d * partialTick;
}

void LivingEntity::setRotation(float newYaw, float newPitch) {
    yaw = oldYaw = bodyYaw = oldBodyYaw = headYaw = oldHeadYaw = newYaw;
    pitch = oldPitch = newPitch;
}

void LivingEntity::beginLivingTick() {
    ++tickCount;
    oldAttackAnim_ = attackAnim_;
    oldBodyYaw = bodyYaw;
    oldHeadYaw = headYaw;
}

void LivingEntity::updateSwingTime() {
    if (swinging_) {
        if (++swingTime_ >= kSwingDuration) {
            swingTime_ = 0;
            swinging_ = false;
        }
    } else {
        swingTime_ = 0;
    }
    attackAnim_ = float(swingTime_) / float(kSwingDuration);
}

void LivingEntity::updateWalkAnimation(bool includeHeight) {
    const glm::dvec3 d = pos - oldPos;
    const float distance = float(std::sqrt(d.x * d.x + (includeHeight ? d.y * d.y : 0.0) + d.z * d.z));
    walkAnimation.update(std::min(distance * 4.0f, 1.0f), 0.4f);
}

void LivingEntity::turnBody() {
    constexpr float kRadToDeg = 180.0f / 3.14159265358979f;
    const double dx = pos.x - oldPos.x, dz = pos.z - oldPos.z;
    float target = bodyYaw;
    if (float(dx * dx + dz * dz) > 0.0025000002f) {
        // Moving: face the movement, or away from it when walking backwards.
        const float moveYaw = float(std::atan2(dz, dx)) * kRadToDeg - 90.0f;
        const float diff = std::abs(wrapDegrees(yaw) - moveYaw);
        target = 95.0f < diff && diff < 265.0f ? moveYaw - 180.0f : moveYaw;
    }
    if (attackAnim_ > 0.0f) target = yaw;
    // Vanilla tickHeadTurn.
    bodyYaw += wrapDegrees(target - bodyYaw) * 0.3f;
    const float head = wrapDegrees(yaw - bodyYaw);
    if (std::abs(head) > kMaxHeadRotation) bodyYaw += head - std::copysign(kMaxHeadRotation, head);
}

void LivingEntity::updateArmorValue() {
    float armor = baseArmor;
    float toughness = 0.0f;
    for (const ItemStack& piece : armorItems())
        if (!piece.empty()) {
            armor += float(piece.item->armor);
            toughness += piece.item->toughness;
        }
    armorValue = int(armor);
    armorToughness = toughness;
}

bool LivingEntity::hurt(const DamageSource& source, float amount) {
    const DamageInfo& info = source.info();
    if (dead() || invulnerableTo(source)) return false;
    if (info.fire && fireImmune()) return false;
    if (!preHurt(source, amount)) return false;
    // A helmet takes falling blocks for a quarter of the damage (and the player's wears).
    std::span<ItemStack> armor = armorItems();
    if (info.damagesHelmet && armor.size() == 4 && !armor[3].empty()) {
        if (armorWears()) {
            armor[3].hurt(std::max(1, int(amount / 4.0f)));
            updateArmorValue();
        }
        amount *= 0.75f;
    }
    amount = std::max(amount, 0.0f);
    walkAnimation.speed = 1.5f; // the limbs flail
    // Within the second half of the invulnerability window only a harder hit counts, and
    // only by what it adds.
    float dealt = amount;
    const bool fresh = !(invulnerableTime > 10 && !info.bypassesCooldown);
    if (!fresh) {
        if (amount <= lastHurt) return false;
        dealt = amount - lastHurt;
        lastHurt = amount;
    } else {
        lastHurt = amount;
        invulnerableTime = 20;
        hurtDuration = 10;
        hurtTime = hurtDuration;
    }
    // actuallyHurt: armor takes a quarter of the damage as wear (at least 1; only the
    // player's), then reduces it.
    if (!info.bypassesArmor) {
        if (armorWears()) {
            const int wear = std::max(1, int(dealt / 4.0f));
            for (ItemStack& piece : armor)
                if (!piece.empty() && !(info.fire && fireResistant(*piece.item))) piece.hurt(wear);
        }
        updateArmorValue();
        dealt = damageAfterArmor(dealt, float(armorValue), armorToughness);
    }
    if (dealt != 0.0f) {
        causeHurtExhaustion(info.exhaustion);
        health = std::max(0.0f, health - dealt);
        lastDamage = source.type;
        lastAttackerType = source.attacker ? std::string(source.attacker->isPlayer() ? "minecraft:player"
                                                                                       : source.attacker->type())
                                           : std::string();
    }
    // Who did it (vanilla resolveMobResponsibleForDamage, resolvePlayerResponsibleForDamage).
    if (const auto* attacker = dynamic_cast<const LivingEntity*>(source.attacker)) {
        lastHurtByMob = attacker->id;
        lastHurtByMobTime = tickCount;
        if (attacker->isPlayer()) lastHurtByPlayerTime = 100;
    }
    lastHurtType = source.type;
    lastHurtTick = tickCount;
    // Attacks push away from where they came from, arrows along their flight.
    if (fresh && info.knockback) {
        double x = 0.0, z = 0.0;
        if (info.projectile && source.direct) {
            x = -source.direct->velocity.x;
            z = -source.direct->velocity.z;
        } else if (source.position) {
            x = source.position->x - pos.x;
            z = source.position->z - pos.z;
        }
        knockback(0.4, x, z);
    }
    return true;
}

void LivingEntity::knockback(double strength, double x, double z) {
    strength *= 1.0 - double(knockbackResistance);
    if (strength <= 0.0) return;
    // Vanilla picks a random tiny direction when there is none (with Math.random).
    while (x * x + z * z < 1.0e-5) {
        x = (random.nextDouble() - random.nextDouble()) * 0.01;
        z = (random.nextDouble() - random.nextDouble()) * 0.01;
    }
    const glm::dvec2 push = glm::normalize(glm::dvec2(x, z)) * strength;
    velocity = {velocity.x / 2.0 - push.x, onGround ? std::min(0.4, velocity.y / 2.0 + strength) : velocity.y,
                velocity.z / 2.0 - push.y};
}

void LivingEntity::heal(float amount) {
    if (!dead()) health = std::min(maxHealth, health + amount);
}

void LivingEntity::igniteForTicks(int ticks) { remainingFireTicks = std::max(remainingFireTicks, ticks); }

std::string_view LivingEntity::deathMessageKey() const {
    if (lastDamage == DamageType::Fall && lastFallDistance > 5.0f) return "death.fell.accident.generic";
    return damageInfo(lastDamage).deathMessage;
}

bool LivingEntity::eyeInWater(const BlockGetter& level) const {
    // Vanilla updateFluidOnEyes: the fluid's surface above the eyes (less 1/9).
    const double eyeY = pos.y + eyeHeight() - 0.11111111;
    const BlockPos p = BlockPos::containing({pos.x, eyeY, pos.z});
    const FluidState f = level.fluid(p);
    if (f.kind != FluidKind::Water) return false;
    const bool full = level.fluid(p.offset(0, 1, 0)).kind == FluidKind::Water;
    const double amount = f.falling || f.source() ? 8.0 : 8.0 - double(f.level);
    return double(p.y) + (full ? 1.0 : amount / 9.0) > eyeY;
}

bool LivingEntity::inWall(const BlockGetter& level) const {
    if (noPhysics) return false;
    // Vanilla isInWall: a flat square 0.8 of the width at the eyes inside a solid block.
    const double half = width * 0.8 / 2.0, eyeY = pos.y + eyeHeight();
    const AABB eyes(pos.x - half, eyeY - 5e-7, pos.z - half, pos.x + half, eyeY + 5e-7, pos.z + half);
    for (int y = int(std::floor(eyes.min.y)); y <= int(std::floor(eyes.max.y)); ++y)
        for (int z = int(std::floor(eyes.min.z)); z <= int(std::floor(eyes.max.z)); ++z)
            for (int x = int(std::floor(eyes.min.x)); x <= int(std::floor(eyes.max.x)); ++x) {
                const StateId s = level.getBlock({x, y, z});
                if (!(level.registry().info(s).flags & kOpaqueCube)) continue;
                for (const AABB& b : level.registry().collisionShape(s))
                    if (b.moved(glm::dvec3(x, y, z)).intersects(eyes)) return true;
            }
    return false;
}

void LivingEntity::environmentTick(const BlockGetter& level) {
    if (dead()) {
        ++deathTime;
        return;
    }
    if (invulnerableTime > 0) --invulnerableTime;
    if (hurtTime > 0) --hurtTime;
    if (lastHurtByPlayerTime > 0) --lastHurtByPlayerTime;
    updateArmorValue();
    const BlockRegistry& reg = level.registry();
    auto blockId = [&](const BlockPos& p) { return shortId(reg.blockOf(level.getBlock(p)).id); };

    // Landing: fall damage past 3 blocks, less on hay, honey and beds, none on slime unless
    // sneaking, double on a stalagmite.
    if (landedFall > 0.0) {
        double fall = landedFall;
        landedFall = 0.0;
        const std::string_view on = blockId(landedOn);
        if (on == "farmland") landedOnFarmland = std::pair(landedOn, fall);
        float multiplier = 1.0f;
        DamageType type = DamageType::Fall;
        // The blocks' fallOn: hay and honey take 80% off, slime all of it (unless sneaking),
        // a bed half the distance, and a stalagmite's tip adds 2.5 to it and doubles the damage.
        if (on == "hay_block" || on == "honey_block") multiplier = 0.2f;
        else if (on == "slime_block" && !steppingCarefully()) multiplier = 0.0f;
        else if (on.ends_with("_bed")) fall *= 0.5;
        else if (on == "pointed_dripstone" && reg.value(level.getBlock(landedOn), "vertical_direction") == "up" &&
                 reg.value(level.getBlock(landedOn), "thickness") == "tip") {
            fall += 2.5;
            multiplier = 2.0f;
            type = DamageType::Stalagmite;
        }
        onLanded(fall);
        const int damage = int(std::floor((fall + 1.0e-6 - 3.0) * double(multiplier)));
        if (damage > 0 && !fallDamageImmune()) {
            lastFallDistance = float(fall);
            hurt(type, float(damage));
        }
    }
    // Air runs out under water; drowning hurts every second.
    if (eyeInWater(level)) {
        if (!canBreatheUnderwater() && !invulnerableTo(DamageType::Drown) && --airSupply <= -20) {
            airSupply = 0;
            hurt(DamageType::Drown, 2.0f);
        }
    } else if (airSupply < maxAirSupply) {
        airSupply = std::min(airSupply + 4, maxAirSupply);
    }
    // Water and rain put fire out; lava sets it and burns.
    if (inWater || inRain) remainingFireTicks = 0;
    if (inLava && !fireImmune()) {
        igniteForTicks(15 * 20);
        hurt(DamageType::Lava, 4.0f);
        fallDistance *= 0.5;
    }
    if (remainingFireTicks > 0) {
        if (fireImmune()) remainingFireTicks = std::max(0, remainingFireTicks - 4);
        else {
            if (remainingFireTicks % 20 == 0 && !inLava) hurt(DamageType::OnFire, 1.0f);
            --remainingFireTicks;
        }
    }
    // The blocks the body is in (vanilla entityInside) and the one stood on (stepOn).
    const AABB b = box().inflated(-1.0e-7, -1.0e-7, -1.0e-7);
    bool inFireBlock = false, inSoulFire = false, inCactus = false, inBerries = false, inCampfire = false, soulCampfire = false;
    bool inCobweb = false, inBush = false;
    for (int y = int(std::floor(b.min.y)); y <= int(std::floor(b.max.y)); ++y)
        for (int z = int(std::floor(b.min.z)); z <= int(std::floor(b.max.z)); ++z)
            for (int x = int(std::floor(b.min.x)); x <= int(std::floor(b.max.x)); ++x) {
                const StateId s = level.getBlock({x, y, z});
                const std::string_view id = shortId(reg.blockOf(s).id);
                if (id == "fire") inFireBlock = true;
                else if (id == "soul_fire") inSoulFire = true;
                else if (id == "cactus") inCactus = true;
                else if (id == "cobweb") inCobweb = true;
                else if (id == "sweet_berry_bush") {
                    inBush = true;
                    inBerries = inBerries || reg.intValue(s, "age") > 0;
                } else if ((id == "campfire" || id == "soul_campfire") && reg.boolValue(s, "lit")) {
                    inCampfire = true;
                    soulCampfire = soulCampfire || id == "soul_campfire";
                }
            }
    // Cobwebs and berry bushes hold it back (vanilla makeStuckInBlock; spiders ignore webs).
    if (inCobweb && !ignoresCobwebs()) makeStuckInBlock({0.25, 0.05, 0.25});
    else if (inBush) makeStuckInBlock({0.8, 0.75, 0.8});
    if ((inFireBlock || inSoulFire) && !fireImmune()) {
        igniteForTicks(8 * 20);
        hurt(DamageType::InFire, inSoulFire ? 2.0f : 1.0f);
    }
    if (inCampfire) hurt(DamageType::Campfire, soulCampfire ? 2.0f : 1.0f);
    if (inCactus) hurt(DamageType::Cactus, 1.0f);
    if (inBerries) {
        const glm::dvec3 moved = pos - oldPos;
        if (std::abs(moved.x) >= 0.003 || std::abs(moved.z) >= 0.003) hurt(DamageType::SweetBerryBush, 1.0f);
    }
    if (onGround && !steppingCarefully() && blockId(blockBelow()) == "magma_block") hurt(DamageType::HotFloor, 1.0f);
    if (inWall(level)) hurt(DamageType::InWall, 1.0f);
    if (pos.y < double(kWorldMinY - 64)) hurt(DamageType::OutOfWorld, 4.0f);
}

} // namespace mc
