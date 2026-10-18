#include "entity/ExperienceOrb.h"

#include <cmath>

#include "entity/Player.h"
#include "world/level/Level.h"

namespace mc {

ExperienceOrb::ExperienceOrb() {
    width = height = 0.5f;
}

int ExperienceOrb::icon() const {
    static constexpr int kSizes[] = {2477, 1237, 617, 307, 149, 73, 37, 17, 7, 3};
    for (int i = 0; i < 10; ++i)
        if (value >= kSizes[i]) return 10 - i;
    return 0;
}

int ExperienceOrb::splitValue(int amount) {
    static constexpr int kSizes[] = {2477, 1237, 617, 307, 149, 73, 37, 17, 7, 3};
    for (int size : kSizes)
        if (amount >= size) return size;
    return 1;
}

void ExperienceOrb::award(Level& level, const glm::dvec3& at, int amount) {
    Xoroshiro& r = level.random();
    while (amount > 0) {
        const int v = splitValue(amount);
        amount -= v;
        // tryMergeToExisting: an orb of the same value nearby (one id in 40, picked at random) takes it.
        bool merged = false;
        const int pick = r.nextInt(40);
        for (Entity* e : level.entitiesIn(AABB(at - glm::dvec3(0.5), at + glm::dvec3(0.5))))
            if (auto* orb = dynamic_cast<ExperienceOrb*>(e); orb && orb->value == v && (int(orb->id) - pick) % 40 == 0) {
                ++orb->count;
                orb->age = 0;
                merged = true;
                break;
            }
        if (merged) continue;
        auto orb = std::make_unique<ExperienceOrb>();
        orb->pos = orb->oldPos = at;
        orb->value = v;
        orb->yaw = r.nextFloat() * 360.0f;
        orb->velocity = {(r.nextDouble() * 0.2 - 0.1) * 2.0, r.nextDouble() * 0.2 * 2.0, (r.nextDouble() * 0.2 - 0.1) * 2.0};
        level.addEntity(std::move(orb));
    }
}

void ExperienceOrb::tick(Level& level) {
    ++tickCount;
    oldPos = pos;
    updateFluidState(level);
    if (inWater && waterHeight > 0.0) {
        velocity.x *= 0.99f;
        velocity.z *= 0.99f;
        if (velocity.y < 0.06f) velocity.y += 5.0e-4f;
    } else {
        velocity.y -= 0.03; // gravity
    }
    Xoroshiro& r = level.random();
    if (level.fluid(BlockPos::containing(pos)).kind == FluidKind::Lava)
        velocity = {(r.nextFloat() - r.nextFloat()) * 0.2f, 0.2f, (r.nextFloat() - r.nextFloat()) * 0.2f};
    if (insideBlocks(level)) moveTowardsClosestSpace(level, r.nextFloat());
    // Merges every second with equal orbs nearby.
    if (tickCount % 20 == 1)
        for (Entity* e : level.entitiesIn(box().inflated(0.5, 0.5, 0.5), this))
            if (auto* orb = dynamic_cast<ExperienceOrb*>(e);
                orb && !orb->removed && orb->value == value && (int(orb->id) - int(id)) % 40 == 0) {
                count += orb->count;
                age = std::min(age, orb->age);
                orb->removed = true;
            }
    // Toward the nearest player within 8 blocks, faster the closer it gets.
    if (const Player* player = level.player(); player && player->mode() != GameMode::Spectator && player->alive()) {
        const glm::dvec3 to(player->pos.x - pos.x, player->pos.y + player->eyeHeight() / 2.0 - pos.y, player->pos.z - pos.z);
        const double d2 = glm::dot(to, to);
        if (d2 < 64.0 && d2 > 1.0e-8) {
            const double f = 1.0 - std::sqrt(d2) / 8.0;
            velocity += glm::normalize(to) * (f * f * 0.1);
        }
    }
    const double vy = velocity.y;
    move(level, velocity);
    float friction = 0.98f;
    if (onGround) friction = level.registry().blockOf(level.getBlock(blockBelow())).friction * 0.98f;
    velocity *= glm::dvec3(friction, 0.98, friction);
    if (onGround) velocity.y = -vy * 0.4; // bounces
    if (++age >= kLifetime || pos.y < double(kWorldMinY - 64)) removed = true;
}

void ExperienceOrb::playerTouch(Level& level, Player& player) {
    if (removed || player.takeXpDelay > 0) return;
    player.takeXpDelay = 2;
    level.recordPickup(*this, 1);
    player.giveExperiencePoints(value);
    if (--count <= 0) removed = true;
}

bool ExperienceOrb::hurtServer(Level&, DamageType, float amount) {
    health = int(float(health) - amount);
    if (health <= 0) removed = true;
    return true;
}

void ExperienceOrb::saveExtra(Json& out) const {
    out["value"] = value;
    out["count"] = count;
    out["age"] = age;
    out["health"] = health;
}

void ExperienceOrb::loadExtra(const Json& in, const ItemRegistry&) {
    value = std::max(1, in.value("value", 1));
    count = std::max(1, in.value("count", 1));
    age = in.value("age", 0);
    health = in.value("health", 5);
}

} // namespace mc
