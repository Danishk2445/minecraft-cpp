#include "entity/Arrow.h"

#include <climits>
#include <cmath>

#include "core/Rng.h"
#include "entity/ItemEntity.h"
#include "entity/LivingEntity.h"
#include "entity/Physics.h"
#include "entity/Player.h"
#include "world/level/Level.h"
#include "world/level/Raycast.h"

namespace mc {
namespace {

constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;

double triangle(Xoroshiro& r, double deviation) { return deviation * (r.nextDouble() - r.nextDouble()); }

// Vanilla Projectile.lerpRotation: a fifth of the way from the old angle to the new.
float lerpRotation(float previous, float current) {
    while (current - previous < -180.0f) previous -= 360.0f;
    while (current - previous >= 180.0f) previous += 360.0f;
    return previous + (current - previous) * 0.2f;
}

bool contains(const AABB& b, const glm::dvec3& p) {
    return p.x > b.min.x && p.x < b.max.x && p.y > b.min.y && p.y < b.max.y && p.z > b.min.z && p.z < b.max.z;
}

} // namespace

Arrow::Arrow() {
    width = height = 0.5f;
}

void Arrow::shoot(const glm::dvec3& direction, float speed, float inaccuracy, Xoroshiro& random) {
    const double spread = 0.0172275 * double(inaccuracy);
    glm::dvec3 v = glm::normalize(direction);
    v.x += triangle(random, spread);
    v.y += triangle(random, spread);
    v.z += triangle(random, spread);
    velocity = v * double(speed);
    const double horizontal = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
    yaw = oldYaw = float(std::atan2(velocity.x, velocity.z) * kRadToDeg);
    pitch = oldPitch = float(std::atan2(velocity.y, horizontal) * kRadToDeg);
}

void Arrow::shootFromRotation(const LivingEntity& shooter, float shooterPitch, float shooterYaw, float speed,
                              float inaccuracy, Xoroshiro& random) {
    const double p = glm::radians(double(shooterPitch)), y = glm::radians(double(shooterYaw));
    shoot({-std::sin(y) * std::cos(p), -std::sin(p), std::cos(y) * std::cos(p)}, speed, inaccuracy, random);
    // The shooter's own motion carries over (vertically only when airborne).
    const glm::dvec3 moved = shooter.pos - shooter.oldPos;
    velocity += glm::dvec3(moved.x, shooter.onGround ? 0.0 : moved.y, moved.z);
}

bool Arrow::canHit(const Level& level, const Entity& e) const {
    (void)level;
    if (e.removed || !e.isPickable()) return false;
    return owner == 0 || leftOwner || e.id != owner;
}

void Arrow::tick(Level& level) {
    oldPos = pos;
    oldYaw = yaw;
    oldPitch = pitch;
    ++tickCount;
    const BlockPos cell = BlockPos::containing(pos);
    inWater = level.fluid(cell).kind == FluidKind::Water;
    // Out of the shooter's box yet (vanilla checkLeftOwner)?
    if (!leftOwner) {
        const Entity* o = level.entity(owner);
        leftOwner = !o || !box().expandedTowards(velocity).inflated(1.0, 1.0, 1.0).intersects(o->box());
    }
    // Inside a block's collision: stuck.
    const StateId here = level.getBlock(cell);
    if (!(level.registry().info(here).flags & kAir))
        for (const AABB& b : level.registry().collisionShape(here))
            if (contains(b.moved(glm::dvec3(cell.x, cell.y, cell.z)), pos)) inGround = true;
    if (shakeTime > 0) --shakeTime;
    if (inGround) {
        // The block gone: it drops; else it lies there for a minute.
        if (lastState != here && noCollision(level, AABB(pos, pos).inflated(0.06, 0.06, 0.06))) {
            inGround = false;
            velocity = glm::dvec3(0.0);
            life = 0;
        } else if (++life >= 1200) {
            removed = true;
        }
        ++inGroundTime;
        return;
    }
    inGroundTime = 0;
    // Vanilla 1.21.5 order: water's drag before the move, air's after it, then gravity.
    if (inWater) velocity *= 0.6;
    const double horizontal = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
    yaw = lerpRotation(oldYaw, float(std::atan2(velocity.x, velocity.z) * kRadToDeg));
    pitch = lerpRotation(oldPitch, float(std::atan2(velocity.y, horizontal) * kRadToDeg));

    // The first block on the way (collision shapes), then the first entity before it.
    const glm::dvec3 start = pos;
    const double length = glm::length(velocity);
    glm::dvec3 end = pos + velocity;
    std::optional<BlockHit> blockHit;
    if (length > 1e-9) {
        blockHit = raycastBlocks(level, start, velocity / length, length, RayFluid::None, RayShape::Collider);
        if (blockHit) end = blockHit->point;
    }
    Entity* target = nullptr;
    double best = 1e300;
    glm::dvec3 targetPoint{0.0};
    const glm::dvec3 segment = end - start;
    const double segmentLength = glm::length(segment);
    auto consider = [&](Entity& e) {
        if (!canHit(level, e) || segmentLength < 1e-9) return;
        const AABB b = e.box().inflated(0.3, 0.3, 0.3);
        if (contains(b, start)) return; // vanilla AABB.clip: only rays entering the box
        if (auto hit = clipBox(b, start, segment / segmentLength, segmentLength); hit && hit->first < best) {
            best = hit->first;
            target = &e;
            targetPoint = start + segment / segmentLength * hit->first;
        }
    };
    const AABB area = box().expandedTowards(velocity).inflated(1.0, 1.0, 1.0);
    for (Entity* e : level.entitiesIn(area, this)) consider(*e);
    if (Player* p = level.player(); p && p->box().intersects(area)) consider(*p);

    if (target) {
        pos = targetPoint;
        if (hitEntity(level, *target)) return;
    } else if (blockHit) {
        pos = blockHit->point;
        hitBlock(level, blockHit->point, blockHit->pos);
    } else {
        pos = end;
    }
    if (!inWater) velocity *= 0.99;
    if (!inGround) velocity.y -= 0.05;
}

void Arrow::hitBlock(Level& level, const glm::dvec3& at, const BlockPos& block) {
    lastState = level.getBlock(block);
    const glm::dvec3 back(velocity.x > 0 ? 1.0 : velocity.x < 0 ? -1.0 : 0.0, velocity.y > 0 ? 1.0 : velocity.y < 0 ? -1.0 : 0.0,
                          velocity.z > 0 ? 1.0 : velocity.z < 0 ? -1.0 : 0.0);
    pos = at - back * 0.05;
    velocity = glm::dvec3(0.0);
    inGround = true;
    shakeTime = 7;
    crit = false;
}

bool Arrow::hitEntity(Level& level, Entity& target) {
    const double speed = glm::length(velocity);
    int damage = int(std::ceil(std::clamp(speed * baseDamage, 0.0, double(INT_MAX))));
    if (crit) damage = int(std::min<int64_t>(int64_t(level.random().nextInt(damage / 2 + 2)) + damage, INT_MAX));
    const Entity* shooter = level.entity(owner);
    if (target.hurtServer(level, DamageSource::arrow(*this, shooter), float(damage))) {
        removed = true;
        return true;
    }
    // Not hurt (a creative player): it bounces back, slowed.
    velocity *= -0.5 * 0.2;
    yaw += 170.0f + level.random().nextFloat() * 20.0f;
    oldYaw = yaw;
    if (glm::dot(velocity, velocity) < 1.0e-7) {
        if (pickup == Pickup::Allowed && !pickupItem.empty()) {
            ItemEntity& e = ItemEntity::spawn(level, pos + glm::dvec3(0.0, 0.1, 0.0), pickupItem);
            e.pickupDelay = 10;
        }
        removed = true;
        return true;
    }
    return false;
}

void Arrow::playerTouch(Level& level, Player& player) {
    if (removed || !inGround || shakeTime > 0) return;
    bool taken = false;
    if (pickup == Pickup::Allowed) {
        ItemStack copy = pickupItem;
        if (copy.empty()) return;
        taken = player.inventory.add(copy);
    } else if (pickup == Pickup::CreativeOnly) {
        taken = player.invulnerable();
    }
    if (!taken) return;
    level.recordPickup(*this, 1);
    removed = true;
}

void Arrow::saveExtra(Json& out) const {
    out["damage"] = baseDamage;
    out["crit"] = crit;
    out["in_ground"] = inGround;
    out["life"] = life;
    out["shake"] = shakeTime;
    out["pickup"] = int(pickup);
    if (!pickupItem.empty()) out["item"] = stackToJson(pickupItem);
    out["in_block"] = lastState;
}

void Arrow::loadExtra(const Json& in, const ItemRegistry& items) {
    baseDamage = in.value("damage", 2.0);
    crit = in.value("crit", false);
    inGround = in.value("in_ground", false);
    life = in.value("life", 0);
    shakeTime = in.value("shake", 0);
    pickup = Pickup(std::clamp(in.value("pickup", 0), 0, 2));
    if (in.contains("item")) pickupItem = stackFromJson(in["item"], items);
    lastState = StateId(in.value("in_block", 0));
    leftOwner = true;
}

} // namespace mc
