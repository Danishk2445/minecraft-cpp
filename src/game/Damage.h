// Damage.h -- damage types (vanilla's data/damage_type with the tags that matter here)
// and the armor formula (CombatRules.getDamageAfterAbsorb).
#pragma once

#include <string_view>

namespace mc {

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
};

struct DamageInfo {
    std::string_view deathMessage; // translation key, "%1$s ..."
    float exhaustion = 0.0f;       // hunger it causes (vanilla DamageType.exhaustion)
    bool bypassesArmor = false;
    bool bypassesInvulnerability = false; // even creative players take it
    bool fire = false;                    // DamageTypeTags.IS_FIRE (fire-resistant items survive)
    bool bypassesCooldown = false;        // ignores the 10-tick invulnerability window
};
const DamageInfo& damageInfo(DamageType type);

// Damage after armor points and toughness: reduced by clamp(armor - damage / (2 + toughness/4),
// armor / 5, 20) / 25.
float damageAfterArmor(float damage, float armor, float toughness);

} // namespace mc
