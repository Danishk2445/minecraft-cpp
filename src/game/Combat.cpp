#include "game/Combat.h"

#include <cmath>
#include <string>

#include "entity/Physics.h"
#include "entity/Player.h"
#include "entity/mob/Animals.h"
#include "entity/mob/Monsters.h"
#include "game/Item.h"
#include "world/level/Level.h"

namespace mc {
namespace {

constexpr float kDegToRad = 3.14159265358979f / 180.0f;

int packBox(const Entity& e) { return int(e.width * 100.0f) | (int(e.height * 100.0f) << 16); }

} // namespace

AttackResult playerAttack(Level& level, Player& player, Entity& target) {
    AttackResult r;
    ItemStack& held = player.inventory.selectedStack();
    // The attack_damage attribute (1 bare-handed, the weapon's otherwise), scaled by how far
    // the cooldown has come.
    float damage = held.empty() ? 1.0f : held.item->attackDamage;
    const float strength = player.attackStrengthScale(0.5f);
    damage *= 0.2f + strength * strength * 0.8f;
    player.resetAttackStrengthTicker();
    if (damage <= 0.0f) return r;
    const bool full = strength > 0.9f;
    const bool sprintKnockback = player.sprinting && full;
    auto* living = dynamic_cast<LivingEntity*>(&target);
    // Critical: falling, not on a ladder, in water or sprinting.
    r.critical = full && player.fallDistance > 0.0 && !player.onGround && !player.onClimbable(level) && !player.inWater &&
                 living && !player.sprinting;
    if (r.critical) damage *= 1.5f;
    // Sweep: a sword's full-strength hit standing (nearly) still on the ground.
    if (full && !r.critical && !sprintKnockback && player.onGround) {
        const glm::dvec3 moved = player.pos - player.oldPos;
        const double limit = double(player.walkSpeed) * 2.5;
        r.sweep = moved.x * moved.x + moved.z * moved.z < limit * limit && !held.empty() && held.item->tool == ToolKind::Sword;
    }
    r.damage = damage;
    const float before = living ? living->health : 0.0f;
    const DamageSource source = DamageSource::playerAttack(player);
    r.hurt = target.hurtServer(level, source, damage);
    if (!r.hurt) return r;
    const float s = std::sin(player.yaw * kDegToRad), c = std::cos(player.yaw * kDegToRad);
    if (sprintKnockback) {
        r.knockback = true;
        if (living) living->knockback(0.5, s, -c);
        else target.velocity += glm::dvec3(-s * 0.5, 0.1, c * 0.5);
        player.velocity *= glm::dvec3(0.6, 1.0, 0.6);
        player.sprinting = false;
    }
    if (r.sweep) {
        // Everything else beside the target and within 3 blocks of the player: 1 damage
        // (scaled by the cooldown) and a shove.
        for (Entity* e : level.entitiesIn(target.box().inflated(1.0, 0.25, 1.0), &target)) {
            auto* other = dynamic_cast<LivingEntity*>(e);
            if (!other || other->isPlayer() || other->dead()) continue;
            const glm::dvec3 d = other->pos - player.pos;
            if (glm::dot(d, d) >= 9.0) continue;
            other->knockback(0.4, s, -c);
            other->hurtServer(level, source, 1.0f * strength);
        }
        level.levelEvent(LevelEventType::Sweep, BlockPos::containing(player.pos), 0, 0,
                         glm::dvec3(player.pos.x - s, player.pos.y + player.height * 0.5, player.pos.z + c));
    }
    if (r.critical) level.levelEvent(LevelEventType::Crit, BlockPos::containing(target.pos), 0, packBox(target), target.pos);
    if (living) {
        r.dealt = before - living->health;
        if (r.dealt > 2.0f)
            level.levelEvent(LevelEventType::DamageIndicator, BlockPos::containing(target.pos), StateId(int(r.dealt * 0.5f)),
                             packBox(target), target.pos);
    }
    // The weapon wears: swords 1, tools 2 (vanilla's weapon component).
    if (!held.empty() && held.item->maxDamage > 0 && held.item->tool != ToolKind::None &&
        held.item->tool != ToolKind::Shears && !player.invulnerable()) {
        if (held.hurt(held.item->tool == ToolKind::Sword ? 1 : 2)) held = ItemStack{};
    }
    player.causeFoodExhaustion(0.1f);
    return r;
}

std::optional<std::string> spawnEggType(const Item& item) {
    std::string_view id = item.id;
    if (!id.ends_with("_spawn_egg")) return std::nullopt;
    return std::string(id.substr(0, id.size() - 10));
}

Mob* spawnFromEgg(Level& level, const Item& egg, const BlockPos& clicked, Direction face) {
    const auto type = spawnEggType(egg);
    const EntityTypes* types = level.entityTypes();
    if (!type || !types) return nullptr;
    std::unique_ptr<Mob> mob = createMob(*types, *type);
    if (!mob) return nullptr;
    mob->random = Xoroshiro(int64_t(level.random().nextLong()));
    const BlockRegistry& reg = level.registry();
    const BlockPos at = reg.collisionShape(level.getBlock(clicked)).empty() ? clicked : clicked.offset(face);
    // Vanilla EntityType.getYOffset: from a block up, down onto what's in its cell (and,
    // clicking a top face, the cell below).
    const bool further = at != clicked && face == Direction::Up;
    mob->pos = {at.x + 0.5, double(at.y + 1), at.z + 0.5};
    AABB cell(at.x, at.y, at.z, at.x + 1.0, at.y + 1.0, at.z + 1.0);
    if (further) cell.min.y -= 1.0;
    std::vector<AABB> shapes;
    gatherBlockCollisions(level, cell, shapes);
    std::erase_if(shapes, [&](const AABB& b) { return !b.intersects(cell); });
    const double drop = collideAxis(1, mob->box(), shapes, further ? -2.0 : -1.0);
    mob->pos.y = double(at.y) + 1.0 + drop;
    mob->oldPos = mob->pos;
    mob->setRotation(wrapDegrees(level.random().nextFloat() * 360.0f), 0.0f);
    SpawnGroup group;
    mob->finalizeSpawn(level, SpawnReason::SpawnEgg, group);
    return static_cast<Mob*>(&level.addEntity(std::move(mob)));
}

Mob* spawnBabyFromEgg(Level& level, Mob& parent, const Item& egg) {
    const auto type = spawnEggType(egg);
    if (!type || *type != parent.type()) return nullptr;
    std::unique_ptr<Mob> baby;
    if (auto* animal = dynamic_cast<Animal*>(&parent)) {
        std::unique_ptr<Animal> child = animal->breedOffspring(level, *animal);
        if (!child) return nullptr;
        child->setAge(Animal::kBabyAge);
        baby = std::move(child);
    } else if (dynamic_cast<Zombie*>(&parent) && level.entityTypes()) {
        baby = createMob(*level.entityTypes(), *type);
        if (auto* z = dynamic_cast<Zombie*>(baby.get())) z->setBaby(true);
    }
    if (!baby || !baby->isBaby()) return nullptr;
    baby->random = Xoroshiro(int64_t(level.random().nextLong()));
    baby->pos = baby->oldPos = parent.pos;
    baby->setRotation(0.0f, 0.0f);
    return static_cast<Mob*>(&level.addEntity(std::move(baby)));
}

} // namespace mc
