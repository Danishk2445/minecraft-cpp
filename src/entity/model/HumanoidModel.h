// HumanoidModel.h -- vanilla HumanoidModel.setupAnim and the LivingEntityRenderer
// transform: poses a humanoid model (the player; zombies and skeletons in M12)
// for one frame and places it in the world.
#pragma once

#include <glm/glm.hpp>

#include "entity/model/ModelPart.h"

namespace mc {

class LivingEntity;

// What setupAnim reads (vanilla HumanoidRenderState). Angles in degrees.
struct HumanoidPose {
    float ageInTicks = 0.0f;
    float bodyYaw = 0.0f;
    float headYaw = 0.0f;   // relative to the body
    float headPitch = 0.0f;
    float walkPosition = 0.0f, walkSpeed = 0.0f;
    float attackTime = 0.0f;    // swing progress 0..1
    bool attackLeftArm = false; // the swing is the off hand's (left arm for a right-handed player)
    bool crouching = false;
    bool rightArmItem = false, leftArmItem = false; // ArmPose.ITEM instead of EMPTY
    bool bowDrawn = false;      // ArmPose.BOW_AND_ARROW on the right arm: both arms aim along the look
    bool bodyHidden = false;    // spectators: only the head is drawn
};

// The pose of `entity` at `partialTick` (vanilla extractRenderState).
HumanoidPose humanoidPoseOf(const LivingEntity& entity, float partialTick);

// Copies poses and visibility from `rest` onto `part` (same structure): vanilla resetPose.
void resetPose(ModelPart& part, const ModelPart& rest);
// Vanilla HumanoidModel.setupAnim on a model at its rest pose (parts head, body,
// right_arm, left_arm, right_leg, left_leg).
void animateHumanoid(ModelPart& root, const HumanoidPose& pose);

// Model root space to world space relative to the entity's feet: vanilla
// LivingEntityRenderer.render with setupRotations (yaw 180 - body), the (-1, -1, 1)
// flip, the renderer's `scale` and the -1.501 drop; `yOffset` is the render offset
// (crouching players sit 2/16 lower).
glm::mat4 livingModelTransform(float bodyYaw, float scale, float yOffset = 0.0f, float flipDegrees = 0.0f);
// Vanilla setupRotations: the dying fall over sideways, all the way (90 degrees) in about
// 12 ticks; `deathTime` includes the partial tick, 0 when alive.
float deathFlipDegrees(float deathTime);

} // namespace mc
