#include "entity/ai/Controls.h"

#include <cmath>

#include "entity/mob/Mob.h"
#include "entity/path/PathFinder.h"
#include "world/level/Level.h"

namespace mc {
namespace {

constexpr float kDegToRad = 3.14159265358979f / 180.0f;
constexpr float kRadToDeg = 180.0f / 3.14159265358979f;

} // namespace

float rotateTowards(float from, float to, float maxDelta) {
    return from + std::clamp(wrapDegrees(to - from), -maxDelta, maxDelta);
}

float rotateIfNecessary(float current, float target, float max) {
    return target - std::clamp(wrapDegrees(target - current), -max, max);
}

float rotlerpWrapped(float from, float to, float max) {
    float f = std::clamp(wrapDegrees(to - from), -max, max);
    float out = from + f;
    if (out < 0.0f) out += 360.0f;
    else if (out > 360.0f) out -= 360.0f;
    return out;
}

void MoveControl::tick(Level& level) {
    Mob& mob = mob_;
    if (op_ == Op::Strafe) {
        // Vanilla MoveControl STRAFE: walk sideways and forward relative to the facing, unless
        // that leads off something walkable.
        const float speed = float(speedModifier_) * mob.movementSpeed;
        float forwards = strafeForwards_, right = strafeRight_;
        float length = std::sqrt(forwards * forwards + right * right);
        if (length < 1.0f) length = 1.0f;
        length = speed / length;
        forwards *= length;
        right *= length;
        const float s = std::sin(mob.yaw * kDegToRad), c = std::cos(mob.yaw * kDegToRad);
        const float dx = forwards * c - right * s, dz = right * c + forwards * s;
        const BlockPos at = BlockPos::containing({mob.pos.x + double(dx), mob.pos.y, mob.pos.z + double(dz)});
        if (mobPathType(level, mob.pathMob(level), {at.x, int(std::floor(mob.pos.y)), at.z}) != PathType::Walkable) {
            strafeForwards_ = 1.0f;
            strafeRight_ = 0.0f;
        }
        mob.speed = speed;
        mob.zza = strafeForwards_;
        mob.xxa = strafeRight_;
        op_ = Op::Wait;
    } else if (op_ == Op::MoveTo) {
        op_ = Op::Wait;
        const double dx = wanted_.x - mob.pos.x, dz = wanted_.z - mob.pos.z, dy = wanted_.y - mob.pos.y;
        if (dx * dx + dy * dy + dz * dz < 2.5000003e-7) {
            mob.zza = 0.0f;
            return;
        }
        const float wantYaw = float(std::atan2(dz, dx)) * kRadToDeg - 90.0f;
        mob.yaw = rotlerpWrapped(mob.yaw, wantYaw, 90.0f);
        mob.speed = float(speedModifier_ * double(mob.movementSpeed));
        mob.zza = mob.speed; // vanilla Mob.setSpeed also sets zza
        // Jump up onto what blocks the way (but not over doors and fences).
        const BlockPos feet = BlockPos::containing(mob.pos);
        const StateId s = level.getBlock(feet);
        const Shape& shape = level.registry().collisionShape(s);
        double top = 0.0;
        for (const AABB& b : shape) top = std::max(top, b.max.y);
        const BlockRegistry& reg = level.registry();
        static thread_local const BlockRegistry* cached = nullptr;
        static thread_local TagId doors = 0, fences = 0;
        if (cached != &reg) {
            cached = &reg;
            doors = reg.tag("doors").value_or(TagId(0));
            fences = reg.tag("fences").value_or(TagId(0));
        }
        if ((dy > double(mob.stepHeight) && dx * dx + dz * dz < double(std::max(1.0f, mob.width))) ||
            (!shape.empty() && mob.pos.y < top + double(feet.y) && !reg.hasTag(s, doors) && !reg.hasTag(s, fences))) {
            mob.jumpControl.jump();
            op_ = Op::Jumping;
        }
    } else if (op_ == Op::Jumping) {
        mob.speed = float(speedModifier_ * double(mob.movementSpeed));
        mob.zza = mob.speed;
        if (mob.onGround) op_ = Op::Wait;
    } else {
        mob.zza = 0.0f;
    }
}

void LookControl::setLookAt(const glm::dvec3& p, float yawSpeed, float pitchSpeed) {
    wanted_ = p;
    yawSpeed_ = yawSpeed;
    pitchSpeed_ = pitchSpeed;
    cooldown_ = 2;
}

void LookControl::setLookAt(const glm::dvec3& p) { setLookAt(p, mob_.headRotSpeed(), mob_.maxHeadXRot()); }

void LookControl::setLookAt(const Entity& e, float yawSpeed, float pitchSpeed) {
    const auto* living = dynamic_cast<const LivingEntity*>(&e);
    const double y = living ? living->eyePos().y : (e.box().min.y + e.box().max.y) / 2.0;
    setLookAt({e.pos.x, y, e.pos.z}, yawSpeed, pitchSpeed);
}

void LookControl::tick() {
    Mob& mob = mob_;
    mob.pitch = 0.0f; // vanilla resetXRotOnTick
    if (cooldown_ > 0) {
        --cooldown_;
        const double dx = wanted_.x - mob.pos.x, dz = wanted_.z - mob.pos.z;
        if (std::abs(dz) > 1.0e-5 || std::abs(dx) > 1.0e-5)
            mob.headYaw = rotateTowards(mob.headYaw, float(std::atan2(dz, dx)) * kRadToDeg - 90.0f, yawSpeed_);
        const double dy = wanted_.y - mob.eyePos().y, horizontal = std::sqrt(dx * dx + dz * dz);
        if (std::abs(dy) > 1.0e-5 || std::abs(horizontal) > 1.0e-5)
            mob.pitch = rotateTowards(mob.pitch, -float(std::atan2(dy, horizontal)) * kRadToDeg, pitchSpeed_);
    } else {
        mob.headYaw = rotateTowards(mob.headYaw, mob.bodyYaw, 10.0f);
    }
    // Vanilla clampHeadRotationToBody: walking, the head keeps within reach of the body.
    if (!mob.navigation.isDone()) mob.headYaw = rotateIfNecessary(mob.headYaw, mob.bodyYaw, mob.maxHeadYRot());
}

void JumpControl::tick() {
    mob_.jumping = jump_;
    jump_ = false;
}

void BodyRotationControl::tick() {
    Mob& mob = mob_;
    const double dx = mob.pos.x - mob.oldPos.x, dz = mob.pos.z - mob.oldPos.z;
    if (dx * dx + dz * dz > 2.5000003e-7) {
        // Moving: the body faces where it goes, the head stays within reach.
        mob.bodyYaw = mob.yaw;
        mob.headYaw = rotateIfNecessary(mob.headYaw, mob.bodyYaw, mob.maxHeadYRot());
        lastStableHeadYaw_ = mob.headYaw;
        headStableTime_ = 0;
        return;
    }
    if (std::abs(mob.headYaw - lastStableHeadYaw_) > 15.0f) {
        headStableTime_ = 0;
        lastStableHeadYaw_ = mob.headYaw;
        mob.bodyYaw = rotateIfNecessary(mob.bodyYaw, mob.headYaw, mob.maxHeadYRot());
    } else if (++headStableTime_ > 10) {
        // The body slowly follows a head that stays turned.
        const float f = std::clamp(float(headStableTime_ - 10) / 10.0f, 0.0f, 1.0f);
        mob.bodyYaw = rotateIfNecessary(mob.bodyYaw, mob.headYaw, mob.maxHeadYRot() * (1.0f - f));
    }
}

bool Sensing::hasLineOfSight(const Level& level, const Entity& e) {
    if (seen_.contains(e.id)) return true;
    if (unseen_.contains(e.id)) return false;
    const bool sees = mob_.hasLineOfSight(level, e);
    (sees ? seen_ : unseen_).insert(e.id);
    return sees;
}

} // namespace mc
