// LivingEntity.h -- what the player and mobs share (vanilla LivingEntity): health and
// damage (invulnerability frames, armor, knockback, who hurt it), fire, air, landing and
// the blocks that hurt, and for their models body and head rotation, the walk animation
// and arm swings.
#pragma once

#include <optional>
#include <span>
#include <string>

#include "core/Rng.h"
#include "entity/Entity.h"
#include "game/Inventory.h"

namespace mc {

// Vanilla WalkAnimationState: limb swing phase (position) and amplitude (speed).
struct WalkAnimation {
    float speedOld = 0.0f, speed = 0.0f, position = 0.0f;

    void update(float movementSpeed, float multiplier) {
        speedOld = speed;
        speed += (movementSpeed - speed) * multiplier;
        position += speed;
    }
    float speedAt(float partialTick) const { return speedOld + (speed - speedOld) * partialTick; }
    float positionAt(float partialTick) const { return position - speed * (1.0f - partialTick); }
};

// Degrees in [-180, 180) (vanilla Mth.wrapDegrees).
float wrapDegrees(float degrees);
// From a toward b the short way round (vanilla Mth.rotLerp).
float rotLerp(float t, float a, float b);

class LivingEntity : public Entity {
public:
    static constexpr int kSwingDuration = 6;           // ticks (vanilla getCurrentSwingDuration)
    static constexpr float kMaxHeadRotation = 50.0f;   // head yaw relative to the body

    float bodyYaw = 0.0f, oldBodyYaw = 0.0f; // yBodyRot
    float headYaw = 0.0f, oldHeadYaw = 0.0f; // yHeadRot
    WalkAnimation walkAnimation;

    // Starts an arm swing (vanilla swing); a running one restarts once half done.
    void swing(bool offhand = false);
    bool swingingOffhand() const { return swingOffhand_; }
    // Swing progress 0..1 (vanilla getAttackAnim).
    float attackAnim(float partialTick) const;
    // Sets every rotation and its previous value at once (spawning, teleports).
    void setRotation(float newYaw, float newPitch);

    // Health and damage (vanilla LivingEntity).
    float health = 20.0f, maxHealth = 20.0f;
    int airSupply = 300, maxAirSupply = 300;
    int remainingFireTicks = 0;
    int invulnerableTime = 0; // after a hit: 20 ticks, the second half blocks weaker hits
    float lastHurt = 0.0f;
    int hurtTime = 0, hurtDuration = 10; // the red flash (and the player's camera shake)
    int deathTime = 0;                    // ticks since dying
    DamageType lastDamage = DamageType::Generic;
    // The type of who dealt the last damage ("minecraft:zombie", "minecraft:player"); empty
    // for the world's damage. The death message names it; loot asks who killed it.
    std::string lastAttackerType;
    float lastFallDistance = 0.0f; // of the last fall that hurt, for the death message
    int armorValue = 0;
    float armorToughness = 0.0f;
    float baseArmor = 0.0f;           // the armor attribute's base (zombies 2)
    float knockbackResistance = 0.0f; // the knockback_resistance attribute, 0..1
    // Vanilla lastHurtByMob (forgotten after 100 ticks) and lastHurtByPlayerMemoryTime:
    // loot and experience drop only after a recent player hit.
    uint32_t lastHurtByMob = 0;
    int lastHurtByMobTime = 0; // tickCount of that hit
    int lastHurtByPlayerTime = 0;
    // The last damage taken and when (vanilla lastDamageSource, forgotten after 40 ticks):
    // what makes animals panic.
    DamageType lastHurtType = DamageType::Generic;
    int lastHurtTick = -1000;
    // The entity's own random (vanilla Entity.random): AI, knockback directions.
    Xoroshiro random{0};
    // A landing on farmland this tick (the block and the fall), for the level to trample.
    std::optional<std::pair<BlockPos, double>> landedOnFarmland;

    bool dead() const { return health <= 0.0f; }
    bool alive() const { return health > 0.0f; }
    bool onFire() const { return remainingFireTicks > 0; }
    // The entity that hurt it within the last 100 ticks, if it is still there.
    uint32_t recentAttacker() const { return tickCount - lastHurtByMobTime <= 100 ? lastHurtByMob : 0; }

    // Armor slots, feet to head (vanilla EquipmentSlot FEET, LEGS, CHEST, HEAD).
    virtual std::span<ItemStack> armorItems() { return {}; }
    // Damage it ignores (creative players: everything but the void and /kill).
    virtual bool invulnerableTo(const DamageSource& source) const {
        (void)source;
        return false;
    }
    virtual bool fireImmune() const { return false; }
    // Vanilla canBreatheUnderwater: the undead don't drown.
    virtual bool canBreatheUnderwater() const { return false; }
    virtual bool fallDamageImmune() const { return false; } // chickens
    // Vanilla isSteppingCarefully: sneaking players don't feel magma.
    virtual bool steppingCarefully() const { return false; }
    virtual bool ignoresCobwebs() const { return false; } // spiders
    // Vanilla getEyeHeight: 0.85 of the height unless the type says otherwise.
    virtual float eyeHeight() const { return height * 0.85f; }
    glm::dvec3 eyePos() const { return pos + glm::dvec3(0.0, eyeHeight(), 0.0); }
    bool isPickable() const override { return !removed && alive(); }

    // Vanilla hurtServer and actuallyHurt: invulnerability frames, a helmet against falling
    // blocks, armor and its wear, knockback from the source, the attacker remembered.
    // True when it hurt.
    bool hurt(const DamageSource& source, float amount);
    bool hurtServer(Level& level, const DamageSource& source, float amount) override {
        (void)level;
        return hurt(source, amount);
    }
    // Vanilla knockback: pushed away from (x, z) by `strength`, less its resistance.
    void knockback(double strength, double x, double z);
    void heal(float amount);
    virtual void igniteForTicks(int ticks);
    // The death message's translation key ("death.attack.drown", "death.fell.accident.generic").
    std::string_view deathMessageKey() const;
    // Vanilla isInWall: the eyes inside a solid block.
    bool inWall(const BlockGetter& level) const;
    // Vanilla isEyeInFluid(WATER): the eyes under the water's surface.
    bool eyeInWater(const BlockGetter& level) const;
    // Recomputes armorValue and armorToughness from the armor slots.
    void updateArmorValue();

protected:
    // Start of a tick: previous values for interpolation (vanilla baseTick).
    void beginLivingTick();
    // Vanilla updateSwingTime.
    void updateSwingTime();
    // Vanilla calculateEntityAnimation: the walk animation follows this tick's movement.
    void updateWalkAnimation(bool includeHeight = false);
    // End of a tick (vanilla LivingEntity.tick after aiStep): the body turns toward the
    // movement, or the look while swinging, and stays within 50 degrees of the head.
    void turnBody();
    // After movement, once per tick (vanilla baseTick and checkFallDamage): the
    // invulnerability countdown, landing, air, fire (put out by water and rain) and lava,
    // the blocks it is in or stands on, the void.
    void environmentTick(const BlockGetter& level);
    // Hooks for hurt(): the player scales damage by difficulty (false: none left), its
    // armor wears, and getting hurt makes it hungry.
    virtual bool preHurt(const DamageSource& source, float& amount) {
        (void)source, (void)amount;
        return true;
    }
    virtual bool armorWears() const { return false; }
    virtual void causeHurtExhaustion(float amount) { (void)amount; }
    // Any landing from a fall (vanilla causeFallDamage): creepers' fuses jump ahead.
    virtual void onLanded(double fallDistance) { (void)fallDistance; }

private:
    bool swinging_ = false, swingOffhand_ = false;
    int swingTime_ = 0;
    float attackAnim_ = 0.0f, oldAttackAnim_ = 0.0f;
};

// The translation key naming an entity in messages ("entity.minecraft.zombie").
std::string entityNameKey(const Entity& e);

} // namespace mc
