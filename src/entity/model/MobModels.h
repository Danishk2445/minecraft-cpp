// MobModels.h -- vanilla layer definitions and setupAnim of the eight mobs: pig, cow,
// sheep (and its wool), chicken, zombie, skeleton, creeper, spider; the humanoid
// armor layers; and vanilla's baby transform (BabyModelTransform).
#pragma once

#include <initializer_list>
#include <string_view>

#include "entity/model/ModelPart.h"

namespace mc {

// QuadrupedModel-based (head, body, four legs).
ModelDefinition pigModel();       // 64x64: legs 6 high, the snout on the head
ModelDefinition cowModel();       // 64x64: horns, muzzle (1.21.5), udder
ModelDefinition sheepModel();     // 64x32
ModelDefinition sheepWoolModel(); // 64x32: SheepFurModel, the wool around it
ModelDefinition chickenModel();   // 64x32: beak and wattle on the head, wings
// HumanoidModel.createMesh(grow) at the given texture size (zombies 64x64; armor 64x32).
ModelDefinition humanoidModel(float grow, int texWidth, int texHeight);
ModelDefinition skeletonModel(); // 64x32: thin limbs
ModelDefinition creeperModel(float grow = 0.0f);
ModelDefinition spiderModel();   // 64x32: eight legs

// Vanilla BabyModelTransform: the `headParts` scaled (when `scaleHead`, by 1.5 / headScale)
// and moved by (0, headY, headZ); the rest scaled by 1 / bodyScale and moved down by bodyY.
ModelDefinition babyModel(const ModelDefinition& adult, bool scaleHead, float headY, float headZ,
                          std::initializer_list<std::string_view> headParts, float headScale = 2.0f,
                          float bodyScale = 2.0f, float bodyY = 24.0f);

// What the mobs' setupAnim reads (vanilla LivingEntityRenderState). Angles in degrees.
struct MobPose {
    float ageInTicks = 0.0f;
    float headYaw = 0.0f; // relative to the body
    float headPitch = 0.0f;
    float walkPosition = 0.0f, walkSpeed = 0.0f;
    float attackTime = 0.0f;
    bool aggressive = false;
    float ageScale = 1.0f;
};

// Vanilla QuadrupedModel.setupAnim: the head looks, the legs trot in diagonal pairs.
void animateQuadruped(ModelPart& root, const MobPose& pose);
// Sheep grazing: the head down by `eatPosition` (0..1) and pitched by `eatAngle` (radians).
void animateSheepHead(ModelPart& root, float eatPosition, float eatAngle, float ageScale);
// Vanilla ChickenModel: legs, and wings flapping by `flap` (radians) and `flapSpeed`.
void animateChicken(ModelPart& root, const MobPose& pose, float flap, float flapSpeed);
void animateCreeper(ModelPart& root, const MobPose& pose);
void animateSpider(ModelPart& root, const MobPose& pose);
// Vanilla HumanoidModel.setupAnim, then the zombie's arms held out (raised when aggressive).
void animateZombie(ModelPart& root, const MobPose& pose);
// Vanilla SkeletonModel: a drawn bow while aggressive with one, zombie-like arms when
// fighting without.
void animateSkeleton(ModelPart& root, const MobPose& pose, bool holdingBow, bool rightArmItem);
// Vanilla HumanoidModel.copyPropertiesTo: the armor layer follows the body it is worn on.
void copyHumanoidPose(ModelPart& armor, const ModelPart& body);

} // namespace mc
