#include "entity/LivingEntity.h"

#include <cmath>

namespace mc {

float wrapDegrees(float degrees) {
    float d = std::fmod(degrees, 360.0f);
    if (d >= 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    return d;
}

float rotLerp(float t, float a, float b) { return a + t * wrapDegrees(b - a); }

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

} // namespace mc
