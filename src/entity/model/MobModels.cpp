#include "entity/model/MobModels.h"

#include <algorithm>
#include <cmath>

namespace mc {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kDegToRad = kPi / 180.0f;

// Vanilla QuadrupedModel.createBodyMesh: head, body (rotated to lie along z), four legs
// `legHeight` high (the left ones' textures mirrored when asked).
ModelDefinition quadruped(int legHeight, bool mirrorLeft, int texHeight) {
    ModelDefinition d;
    d.texHeight = texHeight;
    ModelPart& root = d.root;
    root.addChild("head", {{0.0f, float(18 - legHeight), -6.0f}}).box({0, 0}, {-4, -4, -8}, {8, 8, 8});
    root.addChild("body", {{0.0f, float(17 - legHeight), 2.0f}, {kPi / 2.0f, 0.0f, 0.0f}})
        .box({28, 8}, {-5, -10, -7}, {10, 16, 8});
    const float y = float(24 - legHeight);
    auto leg = [&](const char* name, float x, float z, bool mirror) {
        root.addChild(name, {{x, y, z}}).box({0, 16}, {-2, 0, -2}, {4, float(legHeight), 4}, 0.0f, mirror);
    };
    leg("right_hind_leg", -3.0f, 7.0f, false);
    leg("left_hind_leg", 3.0f, 7.0f, mirrorLeft);
    leg("right_front_leg", -3.0f, -5.0f, false);
    leg("left_front_leg", 3.0f, -5.0f, mirrorLeft);
    return d;
}

void bob(ModelPart& part, float ageInTicks, float scale) {
    part.pose.rotation.z += scale * (std::cos(ageInTicks * 0.09f) * 0.05f + 0.05f);
    part.pose.rotation.x += scale * std::sin(ageInTicks * 0.067f) * 0.05f;
}

void lookHead(ModelPart& root, const MobPose& p) {
    ModelPart& head = *root.find("head");
    head.pose.rotation.x = p.headPitch * kDegToRad;
    head.pose.rotation.y = p.headYaw * kDegToRad;
}

// Vanilla AnimationUtils.animateZombieArms.
void zombieArms(ModelPart& left, ModelPart& right, bool aggressive, float attackTime, float ageInTicks) {
    const float f = std::sin(attackTime * kPi);
    const float f1 = std::sin((1.0f - (1.0f - attackTime) * (1.0f - attackTime)) * kPi);
    right.pose.rotation.z = 0.0f;
    left.pose.rotation.z = 0.0f;
    right.pose.rotation.y = -(0.1f - f * 0.6f);
    left.pose.rotation.y = 0.1f - f * 0.6f;
    const float f2 = -kPi / (aggressive ? 1.5f : 2.25f);
    right.pose.rotation.x = f2 + f * 1.2f - f1 * 0.4f;
    left.pose.rotation.x = f2 + f * 1.2f - f1 * 0.4f;
    bob(right, ageInTicks, 1.0f);
    bob(left, ageInTicks, -1.0f);
}

} // namespace

ModelDefinition pigModel() {
    // Vanilla PigModel: QuadrupedModel with 6-high legs, the snout on the head.
    ModelDefinition d = quadruped(6, true, 64);
    d.root.addChild("head", {{0.0f, 12.0f, -6.0f}})
        .box({0, 0}, {-4, -4, -8}, {8, 8, 8})
        .box({16, 16}, {-2, 0, -9}, {4, 3, 1});
    return d;
}

ModelDefinition cowModel() {
    // Vanilla CowModel.createBaseCowModel (1.21.5): head with muzzle and horns, body with
    // udder, 12-high legs (the left ones mirrored).
    ModelDefinition d;
    d.texHeight = 64;
    ModelPart& root = d.root;
    root.addChild("head", {{0.0f, 4.0f, -8.0f}})
        .box({0, 0}, {-4, -4, -6}, {8, 8, 6})
        .box({1, 33}, {-3, 1, -7}, {6, 3, 1})
        .box({22, 0}, {-5, -5, -5}, {1, 3, 1})
        .box({22, 0}, {4, -5, -5}, {1, 3, 1});
    root.addChild("body", {{0.0f, 5.0f, 2.0f}, {kPi / 2.0f, 0.0f, 0.0f}})
        .box({18, 4}, {-6, -10, -7}, {12, 18, 10})
        .box({52, 0}, {-2, 2, -8}, {4, 6, 1});
    auto leg = [&](const char* name, float x, float z, bool mirror) {
        root.addChild(name, {{x, 12.0f, z}}).box({0, 16}, {-2, 0, -2}, {4, 12, 4}, 0.0f, mirror);
    };
    leg("right_hind_leg", -4.0f, 7.0f, false);
    leg("left_hind_leg", 4.0f, 7.0f, true);
    leg("right_front_leg", -4.0f, -5.0f, false);
    leg("left_front_leg", 4.0f, -5.0f, true);
    return d;
}

ModelDefinition sheepModel() {
    ModelDefinition d = quadruped(12, false, 32);
    d.root.addChild("head", {{0.0f, 6.0f, -8.0f}}).box({0, 0}, {-3, -4, -6}, {6, 6, 8});
    d.root.addChild("body", {{0.0f, 5.0f, 2.0f}, {kPi / 2.0f, 0.0f, 0.0f}}).box({28, 8}, {-4, -10, -7}, {8, 16, 6});
    return d;
}

ModelDefinition sheepWoolModel() {
    // Vanilla SheepFurModel: inflated boxes around head, body and the upper legs.
    ModelDefinition d;
    d.texHeight = 32;
    ModelPart& root = d.root;
    root.addChild("head", {{0.0f, 6.0f, -8.0f}}).box({0, 0}, {-3, -4, -4}, {6, 6, 6}, 0.6f);
    root.addChild("body", {{0.0f, 5.0f, 2.0f}, {kPi / 2.0f, 0.0f, 0.0f}}).box({28, 8}, {-4, -10, -7}, {8, 16, 6}, 1.75f);
    auto leg = [&](const char* name, float x, float z) {
        root.addChild(name, {{x, 12.0f, z}}).box({0, 16}, {-2, 0, -2}, {4, 6, 4}, 0.5f);
    };
    leg("right_hind_leg", -3.0f, 7.0f);
    leg("left_hind_leg", 3.0f, 7.0f);
    leg("right_front_leg", -3.0f, -5.0f);
    leg("left_front_leg", 3.0f, -5.0f);
    return d;
}

ModelDefinition chickenModel() {
    ModelDefinition d;
    d.texHeight = 32;
    ModelPart& root = d.root;
    ModelPart& head = root.addChild("head", {{0.0f, 15.0f, -4.0f}});
    head.box({0, 0}, {-2, -6, -2}, {4, 6, 3});
    head.addChild("beak").box({14, 0}, {-2, -4, -4}, {4, 2, 2});
    head.addChild("red_thing").box({14, 4}, {-1, -2, -3}, {2, 2, 2});
    root.addChild("body", {{0.0f, 16.0f, 0.0f}, {kPi / 2.0f, 0.0f, 0.0f}}).box({0, 9}, {-3, -4, -3}, {6, 8, 6});
    root.addChild("right_leg", {{-2.0f, 19.0f, 1.0f}}).box({26, 0}, {-1, 0, -3}, {3, 5, 3});
    root.addChild("left_leg", {{1.0f, 19.0f, 1.0f}}).box({26, 0}, {-1, 0, -3}, {3, 5, 3});
    root.addChild("right_wing", {{-4.0f, 13.0f, 0.0f}}).box({24, 13}, {0, 0, -3}, {1, 4, 6});
    root.addChild("left_wing", {{4.0f, 13.0f, 0.0f}}).box({24, 13}, {-1, 0, -3}, {1, 4, 6});
    return d;
}

ModelDefinition humanoidModel(float grow, int texWidth, int texHeight) {
    // Vanilla HumanoidModel.createMesh: the left limbs mirror the right ones' texture.
    ModelDefinition d;
    d.texWidth = texWidth;
    d.texHeight = texHeight;
    ModelPart& root = d.root;
    ModelPart& head = root.addChild("head");
    head.box({0, 0}, {-4, -8, -4}, {8, 8, 8}, grow);
    head.addChild("hat").box({32, 0}, {-4, -8, -4}, {8, 8, 8}, grow + 0.5f);
    root.addChild("body").box({16, 16}, {-4, 0, -2}, {8, 12, 4}, grow);
    root.addChild("right_arm", {{-5, 2, 0}}).box({40, 16}, {-3, -2, -2}, {4, 12, 4}, grow);
    root.addChild("left_arm", {{5, 2, 0}}).box({40, 16}, {-1, -2, -2}, {4, 12, 4}, grow, true);
    root.addChild("right_leg", {{-1.9f, 12, 0}}).box({0, 16}, {-2, 0, -2}, {4, 12, 4}, grow);
    root.addChild("left_leg", {{1.9f, 12, 0}}).box({0, 16}, {-2, 0, -2}, {4, 12, 4}, grow, true);
    return d;
}

ModelDefinition skeletonModel() {
    ModelDefinition d = humanoidModel(0.0f, 64, 32);
    ModelPart& root = d.root;
    root.addChild("right_arm", {{-5, 2, 0}}).box({40, 16}, {-1, -2, -1}, {2, 12, 2});
    root.addChild("left_arm", {{5, 2, 0}}).box({40, 16}, {-1, -2, -1}, {2, 12, 2}, 0.0f, true);
    root.addChild("right_leg", {{-2, 12, 0}}).box({0, 16}, {-1, 0, -1}, {2, 12, 2});
    root.addChild("left_leg", {{2, 12, 0}}).box({0, 16}, {-1, 0, -1}, {2, 12, 2}, 0.0f, true);
    return d;
}

ModelDefinition creeperModel(float grow) {
    ModelDefinition d;
    d.texHeight = 32;
    ModelPart& root = d.root;
    root.addChild("head", {{0, 6, 0}}).box({0, 0}, {-4, -8, -4}, {8, 8, 8}, grow);
    root.addChild("body", {{0, 6, 0}}).box({16, 16}, {-4, 0, -2}, {8, 12, 4}, grow);
    auto leg = [&](const char* name, float x, float z) {
        root.addChild(name, {{x, 18, z}}).box({0, 16}, {-2, 0, -2}, {4, 6, 4}, grow);
    };
    leg("right_hind_leg", -2.0f, 4.0f);
    leg("left_hind_leg", 2.0f, 4.0f);
    leg("right_front_leg", -2.0f, -4.0f);
    leg("left_front_leg", 2.0f, -4.0f);
    return d;
}

ModelDefinition spiderModel() {
    ModelDefinition d;
    d.texHeight = 32;
    ModelPart& root = d.root;
    root.addChild("head", {{0, 15, -3}}).box({32, 4}, {-4, -4, -8}, {8, 8, 8});
    root.addChild("body0", {{0, 15, 0}}).box({0, 0}, {-3, -3, -3}, {6, 6, 6});
    root.addChild("body1", {{0, 15, 9}}).box({0, 12}, {-5, -4, -6}, {10, 8, 12});
    constexpr float q = kPi / 4.0f, e = 0.3926991f, m = 0.58119464f;
    auto leg = [&](const char* name, float x, float z, float yRot, float zRot, bool left) {
        root.addChild(name, {{x, 15, z}, {0.0f, yRot, zRot}})
            .box({18, 0}, {left ? -1.0f : -15.0f, -1, -1}, {16, 2, 2}, 0.0f, left);
    };
    leg("right_hind_leg", -4, 2, q, -q, false);
    leg("left_hind_leg", 4, 2, -q, q, true);
    leg("right_middle_hind_leg", -4, 1, e, -m, false);
    leg("left_middle_hind_leg", 4, 1, -e, m, true);
    leg("right_middle_front_leg", -4, 0, -e, -m, false);
    leg("left_middle_front_leg", 4, 0, e, m, true);
    leg("right_front_leg", -4, -1, -q, -q, false);
    leg("left_front_leg", 4, -1, q, q, true);
    return d;
}

ModelDefinition babyModel(const ModelDefinition& adult, bool scaleHead, float headY, float headZ,
                          std::initializer_list<std::string_view> headParts, float headScale, float bodyScale,
                          float bodyY) {
    ModelDefinition d = adult;
    const float headFactor = scaleHead ? 1.5f / headScale : 1.0f, bodyFactor = 1.0f / bodyScale;
    for (ModelPart& part : d.root.children) {
        const bool isHead = std::find(headParts.begin(), headParts.end(), part.name) != headParts.end();
        // PartPose.translated(...).scaled(f): the pivot moves, then scales with the part.
        const glm::vec3 moved = part.pose.offset + (isHead ? glm::vec3(0.0f, headY, headZ) : glm::vec3(0.0f, bodyY, 0.0f));
        const float f = isHead ? headFactor : bodyFactor;
        part.pose.offset = moved * f;
        part.pose.scale *= f;
    }
    return d;
}

void animateQuadruped(ModelPart& root, const MobPose& p) {
    lookHead(root, p);
    const float phase = p.walkPosition * 0.6662f, amount = p.walkSpeed;
    root.find("right_hind_leg")->pose.rotation.x = std::cos(phase) * 1.4f * amount;
    root.find("left_hind_leg")->pose.rotation.x = std::cos(phase + kPi) * 1.4f * amount;
    root.find("right_front_leg")->pose.rotation.x = std::cos(phase + kPi) * 1.4f * amount;
    root.find("left_front_leg")->pose.rotation.x = std::cos(phase) * 1.4f * amount;
}

void animateSheepHead(ModelPart& root, float eatPosition, float eatAngle, float ageScale) {
    ModelPart& head = *root.find("head");
    head.pose.offset.y += eatPosition * 9.0f * ageScale;
    head.pose.rotation.x = eatAngle;
}

void animateChicken(ModelPart& root, const MobPose& p, float flap, float flapSpeed) {
    lookHead(root, p);
    const float f = (std::sin(flap) + 1.0f) * flapSpeed;
    const float phase = p.walkPosition * 0.6662f;
    root.find("right_leg")->pose.rotation.x = std::cos(phase) * 1.4f * p.walkSpeed;
    root.find("left_leg")->pose.rotation.x = std::cos(phase + kPi) * 1.4f * p.walkSpeed;
    root.find("right_wing")->pose.rotation.z = f;
    root.find("left_wing")->pose.rotation.z = -f;
}

void animateCreeper(ModelPart& root, const MobPose& p) {
    lookHead(root, p);
    const float phase = p.walkPosition * 0.6662f, amount = p.walkSpeed;
    root.find("right_hind_leg")->pose.rotation.x = std::cos(phase) * 1.4f * amount;
    root.find("left_hind_leg")->pose.rotation.x = std::cos(phase + kPi) * 1.4f * amount;
    root.find("right_front_leg")->pose.rotation.x = std::cos(phase + kPi) * 1.4f * amount;
    root.find("left_front_leg")->pose.rotation.x = std::cos(phase) * 1.4f * amount;
}

void animateSpider(ModelPart& root, const MobPose& p) {
    lookHead(root, p);
    const float f = p.walkPosition * 0.6662f, s = p.walkSpeed;
    const float swing[4] = {-(std::cos(f * 2.0f) * 0.4f) * s, -(std::cos(f * 2.0f + kPi) * 0.4f) * s,
                            -(std::cos(f * 2.0f + kPi / 2.0f) * 0.4f) * s, -(std::cos(f * 2.0f + kPi * 1.5f) * 0.4f) * s};
    const float lift[4] = {std::abs(std::sin(f) * 0.4f) * s, std::abs(std::sin(f + kPi) * 0.4f) * s,
                           std::abs(std::sin(f + kPi / 2.0f) * 0.4f) * s, std::abs(std::sin(f + kPi * 1.5f) * 0.4f) * s};
    static constexpr const char* kRight[4] = {"right_hind_leg", "right_middle_hind_leg", "right_middle_front_leg",
                                              "right_front_leg"};
    static constexpr const char* kLeft[4] = {"left_hind_leg", "left_middle_hind_leg", "left_middle_front_leg",
                                             "left_front_leg"};
    for (int i = 0; i < 4; ++i) {
        ModelPart& r = *root.find(kRight[i]);
        ModelPart& l = *root.find(kLeft[i]);
        r.pose.rotation.y += swing[i];
        l.pose.rotation.y -= swing[i];
        r.pose.rotation.z += lift[i];
        l.pose.rotation.z -= lift[i];
    }
}

void animateZombie(ModelPart& root, const MobPose& p) {
    zombieArms(*root.find("left_arm"), *root.find("right_arm"), p.aggressive, p.attackTime, p.ageInTicks);
}

void animateSkeleton(ModelPart& root, const MobPose& p, bool holdingBow, bool rightArmItem) {
    (void)rightArmItem;
    ModelPart& right = *root.find("right_arm");
    ModelPart& left = *root.find("left_arm");
    if (p.aggressive && !holdingBow) {
        // Fighting bare-handed (or with a sword): the zombie's reach, flatter.
        const float f = p.attackTime;
        const float f1 = std::sin(f * kPi), f2 = std::sin((1.0f - (1.0f - f) * (1.0f - f)) * kPi);
        right.pose.rotation.z = left.pose.rotation.z = 0.0f;
        right.pose.rotation.y = -(0.1f - f1 * 0.6f);
        left.pose.rotation.y = 0.1f - f1 * 0.6f;
        right.pose.rotation.x = -kPi / 2.0f - (f1 * 1.2f - f2 * 0.4f);
        left.pose.rotation.x = -kPi / 2.0f - (f1 * 1.2f - f2 * 0.4f);
        bob(right, p.ageInTicks, 1.0f);
        bob(left, p.ageInTicks, -1.0f);
    }
}

void copyHumanoidPose(ModelPart& armor, const ModelPart& body) {
    for (const char* name : {"head", "body", "right_arm", "left_arm", "right_leg", "left_leg"}) {
        ModelPart* a = armor.find(name);
        const ModelPart* b = body.find(name);
        if (a && b) a->pose = b->pose;
    }
    if (ModelPart* hat = armor.find("head/hat"))
        if (const ModelPart* bodyHat = body.find("head/hat")) hat->pose = bodyHat->pose;
}

} // namespace mc
