#include "entity/model/HumanoidModel.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "entity/LivingEntity.h"

namespace mc {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kDegToRad = kPi / 180.0f;

// AnimationUtils.bobModelPart: the idle sway of the arms.
void bob(ModelPart& part, float ageInTicks, float scale) {
    part.pose.rotation.z += scale * (std::cos(ageInTicks * 0.09f) * 0.05f + 0.05f);
    part.pose.rotation.x += scale * std::sin(ageInTicks * 0.067f) * 0.05f;
}

// poseRightArm / poseLeftArm for ArmPose.EMPTY and ArmPose.ITEM.
void poseArm(ModelPart& arm, bool item) {
    if (item) arm.pose.rotation.x = arm.pose.rotation.x * 0.5f - kPi / 10.0f;
    arm.pose.rotation.y = 0.0f;
}

} // namespace

HumanoidPose humanoidPoseOf(const LivingEntity& e, float pt) {
    HumanoidPose s;
    s.ageInTicks = float(e.tickCount) + pt;
    const float head = rotLerp(pt, e.oldHeadYaw, e.headYaw);
    s.bodyYaw = rotLerp(pt, e.oldBodyYaw, e.bodyYaw);
    s.headYaw = wrapDegrees(head - s.bodyYaw);
    s.headPitch = e.oldPitch + (e.pitch - e.oldPitch) * pt;
    s.walkPosition = e.walkAnimation.positionAt(pt);
    s.walkSpeed = e.walkAnimation.speedAt(pt);
    s.attackTime = e.attackAnim(pt);
    s.attackLeftArm = e.swingingOffhand();
    return s;
}

void resetPose(ModelPart& part, const ModelPart& rest) {
    part.pose = rest.pose;
    part.visible = rest.visible;
    for (size_t i = 0; i < part.children.size() && i < rest.children.size(); ++i)
        resetPose(part.children[i], rest.children[i]);
}

void animateHumanoid(ModelPart& root, const HumanoidPose& s) {
    ModelPart& head = *root.find("head");
    ModelPart& body = *root.find("body");
    ModelPart& rightArm = *root.find("right_arm");
    ModelPart& leftArm = *root.find("left_arm");
    ModelPart& rightLeg = *root.find("right_leg");
    ModelPart& leftLeg = *root.find("left_leg");

    head.pose.rotation.x = s.headPitch * kDegToRad;
    head.pose.rotation.y = s.headYaw * kDegToRad;
    const float phase = s.walkPosition * 0.6662f, amount = s.walkSpeed;
    rightArm.pose.rotation.x = std::cos(phase + kPi) * 2.0f * amount * 0.5f;
    leftArm.pose.rotation.x = std::cos(phase) * 2.0f * amount * 0.5f;
    rightLeg.pose.rotation.x = std::cos(phase) * 1.4f * amount;
    leftLeg.pose.rotation.x = std::cos(phase + kPi) * 1.4f * amount;
    rightLeg.pose.rotation.y = 0.005f;
    leftLeg.pose.rotation.y = -0.005f;
    rightLeg.pose.rotation.z = 0.005f;
    leftLeg.pose.rotation.z = -0.005f;
    poseArm(rightArm, s.rightArmItem);
    poseArm(leftArm, s.leftArmItem);
    if (s.bowDrawn) {
        // poseRightArm BOW_AND_ARROW (two-handed: it poses both arms).
        rightArm.pose.rotation.y = -0.1f + head.pose.rotation.y;
        leftArm.pose.rotation.y = 0.1f + head.pose.rotation.y + 0.4f;
        rightArm.pose.rotation.x = -kPi / 2.0f + head.pose.rotation.x;
        leftArm.pose.rotation.x = -kPi / 2.0f + head.pose.rotation.x;
    }

    // setupAttackAnimation: the body twists and the swinging arm chops down.
    if (s.attackTime > 0.0f) {
        const float t = s.attackTime;
        ModelPart& arm = s.attackLeftArm ? leftArm : rightArm;
        float twist = std::sin(std::sqrt(t) * kPi * 2.0f) * 0.2f;
        if (s.attackLeftArm) twist = -twist;
        body.pose.rotation.y = twist;
        rightArm.pose.offset.z = std::sin(twist) * 5.0f;
        rightArm.pose.offset.x = -std::cos(twist) * 5.0f;
        leftArm.pose.offset.z = -std::sin(twist) * 5.0f;
        leftArm.pose.offset.x = std::cos(twist) * 5.0f;
        rightArm.pose.rotation.y += twist;
        leftArm.pose.rotation.y += twist;
        leftArm.pose.rotation.x += twist;
        float eased = 1.0f - t;
        eased *= eased;
        eased *= eased;
        eased = 1.0f - eased;
        const float chop = std::sin(eased * kPi);
        const float lift = std::sin(t * kPi) * -(head.pose.rotation.x - 0.7f) * 0.75f;
        arm.pose.rotation.x -= chop * 1.2f + lift;
        arm.pose.rotation.y += twist * 2.0f;
        arm.pose.rotation.z += std::sin(t * kPi) * -0.4f;
    }

    if (s.crouching) {
        body.pose.rotation.x = 0.5f;
        rightArm.pose.rotation.x += 0.4f;
        leftArm.pose.rotation.x += 0.4f;
        rightLeg.pose.offset.z += 4.0f;
        leftLeg.pose.offset.z += 4.0f;
        head.pose.offset.y += 4.2f;
        body.pose.offset.y += 3.2f;
        leftArm.pose.offset.y += 3.2f;
        rightArm.pose.offset.y += 3.2f;
    }
    bob(rightArm, s.ageInTicks, 1.0f);
    bob(leftArm, s.ageInTicks, -1.0f);

    if (s.bodyHidden) body.visible = rightArm.visible = leftArm.visible = rightLeg.visible = leftLeg.visible = false;
}

glm::mat4 livingModelTransform(float bodyYaw, float scale, float yOffset, float flipDegrees) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, yOffset, 0.0f));
    m = glm::rotate(m, (180.0f - bodyYaw) * kDegToRad, glm::vec3(0, 1, 0));
    if (flipDegrees != 0.0f) m = glm::rotate(m, flipDegrees * kDegToRad, glm::vec3(0, 0, 1));
    m = glm::scale(m, glm::vec3(-scale, -scale, scale));
    return glm::translate(m, glm::vec3(0.0f, -1.501f, 0.0f));
}

float deathFlipDegrees(float deathTime) {
    if (deathTime <= 0.0f) return 0.0f;
    return std::min(std::sqrt(std::max(deathTime - 1.0f, 0.0f) / 20.0f * 1.6f), 1.0f) * 90.0f;
}

} // namespace mc
