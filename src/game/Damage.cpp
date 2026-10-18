#include "game/Damage.h"

#include <algorithm>
#include <array>

#include "entity/Entity.h"

namespace mc {

const DamageInfo& damageInfo(DamageType type) {
    // Vanilla damage_type data: message ids, exhaustion; tags bypasses_armor,
    // bypasses_invulnerability, is_fire, bypasses_cooldown, damages_helmet; scaling;
    // no_knockback (everything but attacks), is_projectile, is_explosion.
    using S = DamageScaling;
    static const std::array<DamageInfo, size_t(DamageType::Count)> kInfo = {{
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
        {"death.attack.anvil", 0.1f, false, false, false, false, true},
        {"death.attack.fallingBlock", 0.1f, false, false, false, false, true},
        {"death.attack.explosion", 0.1f, false, false, false, false, false, S::Always, false, false, true},
        {"death.attack.lightningBolt", 0.1f},
        {"death.attack.mob", 0.1f, false, false, false, false, false, S::WhenCausedByMob, true},
        {"death.attack.player", 0.1f, false, false, false, false, false, S::WhenCausedByMob, true},
        {"death.attack.arrow", 0.1f, false, false, false, false, false, S::WhenCausedByMob, true, true},
        {"death.attack.explosion.player", 0.1f, false, false, false, false, false, S::Always, false, false, true},
    }};
    return kInfo[size_t(type)];
}

DamageSource DamageSource::mobAttack(const Entity& mob) {
    DamageSource s(DamageType::MobAttack);
    s.attacker = s.direct = &mob;
    s.position = mob.pos;
    return s;
}

DamageSource DamageSource::playerAttack(const Entity& player) {
    DamageSource s(DamageType::PlayerAttack);
    s.attacker = s.direct = &player;
    s.position = player.pos;
    return s;
}

DamageSource DamageSource::arrow(const Entity& arrow, const Entity* owner) {
    DamageSource s(DamageType::Arrow);
    s.direct = &arrow;
    s.attacker = owner ? owner : &arrow;
    s.position = arrow.pos;
    return s;
}

DamageSource DamageSource::explosion(const Entity* source) {
    DamageSource s(source ? DamageType::PlayerExplosion : DamageType::Explosion);
    s.attacker = s.direct = source;
    return s;
}

float damageAfterArmor(float damage, float armor, float toughness) {
    const float f = 2.0f + toughness / 4.0f;
    const float effective = std::clamp(armor - damage / f, armor * 0.2f, 20.0f);
    return damage * (1.0f - effective / 25.0f);
}

} // namespace mc
