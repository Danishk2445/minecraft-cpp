// LivingEntity.h -- what living entities share for their models: body and head
// rotation, the walk animation and arm swings (vanilla LivingEntity).
#pragma once

#include "entity/Entity.h"

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

private:
    bool swinging_ = false, swingOffhand_ = false;
    int swingTime_ = 0;
    float attackAnim_ = 0.0f, oldAttackAnim_ = 0.0f;
};

} // namespace mc
