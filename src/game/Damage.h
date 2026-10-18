// Damage.h -- damage types (vanilla's data/damage_type with the tags that matter here),
// damage sources (who did it) and the armor formula (CombatRules.getDamageAfterAbsorb).
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include <glm/glm.hpp>

namespace mc {

class Entity;

enum class DamageType : uint8_t {
    Generic,      // /kill uses GenericKill
    GenericKill,  // bypasses invulnerability and armor
    Fall,
    Stalagmite,   // falling on pointed dripstone
    Drown,
    InFire,       // standing in fire
    OnFire,       // burning
    Lava,
    HotFloor,     // magma blocks
    Campfire,
    InWall,       // suffocation
    Cactus,
    SweetBerryBush,
    Starve,
    OutOfWorld,   // the void
    Anvil,        // a falling anvil
    FallingBlock, // other falling blocks that hurt (stalactites)
    Explosion,    // TNT
    LightningBolt,
    MobAttack,       // a mob's melee hit
    PlayerAttack,    // the player's
    Arrow,           // shot by a skeleton or the player
    PlayerExplosion, // an explosion something caused (a creeper)
    Count,
};

// Vanilla DamageScaling: how difficulty scales the damage players take.
enum class DamageScaling : uint8_t { Never, Always, WhenCausedByMob };

struct DamageInfo {
    std::string_view deathMessage; // translation key, "%1$s ..." (and "%2$s" for the attacker)
    float exhaustion = 0.0f;       // hunger it causes (vanilla DamageType.exhaustion)
    bool bypassesArmor = false;
    bool bypassesInvulnerability = false; // even creative players take it
    bool fire = false;                    // DamageTypeTags.IS_FIRE (fire-resistant items survive)
    bool bypassesCooldown = false;        // ignores the 10-tick invulnerability window
    bool damagesHelmet = false;           // DamageTypeTags.DAMAGES_HELMET: a helmet takes a quarter off
    DamageScaling scaling = DamageScaling::Never; // easy halves (+1), hard x1.5, peaceful 0
    bool knockback = false;               // not in DamageTypeTags.NO_KNOCKBACK: the hit pushes away
    bool projectile = false;              // DamageTypeTags.IS_PROJECTILE
    bool explosion = false;               // DamageTypeTags.IS_EXPLOSION
};
const DamageInfo& damageInfo(DamageType type);

// Vanilla DamageSource: the type, the entity responsible (`attacker`: a skeleton for its
// arrow, the creeper for its blast) and the one that hit (`direct`: the arrow). Both are
// only valid during the call that takes the damage.
struct DamageSource {
    DamageType type = DamageType::Generic;
    const Entity* attacker = nullptr;
    const Entity* direct = nullptr;
    // Where the hit came from (the attacker), for knockback; none for the world's damage.
    std::optional<glm::dvec3> position;

    DamageSource() = default;
    DamageSource(DamageType t) : type(t) {} // NOLINT: the world's damage converts implicitly
    static DamageSource mobAttack(const Entity& mob);
    static DamageSource playerAttack(const Entity& player);
    static DamageSource arrow(const Entity& arrow, const Entity* owner);
    // An explosion: caused by `source` (a creeper), or by nothing (TNT).
    static DamageSource explosion(const Entity* source);
    const DamageInfo& info() const { return damageInfo(type); }
};

// Damage after armor points and toughness: reduced by clamp(armor - damage / (2 + toughness/4),
// armor / 5, 20) / 25.
float damageAfterArmor(float damage, float armor, float toughness);

} // namespace mc
