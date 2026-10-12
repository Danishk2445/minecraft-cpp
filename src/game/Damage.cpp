#include "game/Damage.h"

#include <algorithm>
#include <array>

namespace mc {

const DamageInfo& damageInfo(DamageType type) {
    // Vanilla damage_type data: message ids, exhaustion; tags bypasses_armor,
    // bypasses_invulnerability, is_fire, bypasses_cooldown.
    static const std::array<DamageInfo, 15> kInfo = {{
        {"death.attack.generic", 0.0f, true},
        {"death.attack.genericKill", 0.0f, true, true},
        {"death.attack.fall", 0.0f, true},
        {"death.attack.stalagmite", 0.0f, true},
        {"death.attack.drown", 0.0f, true},
        {"death.attack.inFire", 0.1f, false, false, true},
        {"death.attack.onFire", 0.0f, true, false, true},
        {"death.attack.lava", 0.1f, false, false, true},
        {"death.attack.hotFloor", 0.1f, false, false, true},
        {"death.attack.inFire", 0.1f, false, false, true}, // campfire
        {"death.attack.inWall", 0.0f, true},
        {"death.attack.cactus", 0.1f},
        {"death.attack.sweetBerryBush", 0.1f},
        {"death.attack.starve", 0.0f, true},
        {"death.attack.outOfWorld", 0.0f, true, true},
    }};
    return kInfo[size_t(type)];
}

float damageAfterArmor(float damage, float armor, float toughness) {
    const float f = 2.0f + toughness / 4.0f;
    const float effective = std::clamp(armor - damage / f, armor * 0.2f, 20.0f);
    return damage * (1.0f - effective / 25.0f);
}

} // namespace mc
