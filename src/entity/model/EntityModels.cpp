#include "entity/model/EntityModels.h"

namespace mc {

ModelDefinition playerModel(bool slim) {
    ModelDefinition d;
    ModelPart& root = d.root;
    ModelPart& head = root.addChild("head");
    head.box({0, 0}, {-4, -8, -4}, {8, 8, 8});
    head.addChild("hat").box({32, 0}, {-4, -8, -4}, {8, 8, 8}, 0.5f);
    ModelPart& body = root.addChild("body");
    body.box({16, 16}, {-4, 0, -2}, {8, 12, 4});
    body.addChild("jacket").box({16, 32}, {-4, 0, -2}, {8, 12, 4}, 0.25f);
    // Slim arms are 3 wide and hang half a unit lower.
    const float armW = slim ? 3.0f : 4.0f, armY = slim ? 2.5f : 2.0f;
    ModelPart& leftArm = root.addChild("left_arm", {{5, armY, 0}});
    leftArm.box({32, 48}, {-1, -2, -2}, {armW, 12, 4});
    leftArm.addChild("left_sleeve").box({48, 48}, {-1, -2, -2}, {armW, 12, 4}, 0.25f);
    ModelPart& rightArm = root.addChild("right_arm", {{-5, armY, 0}});
    rightArm.box({40, 16}, {slim ? -2.0f : -3.0f, -2, -2}, {armW, 12, 4});
    rightArm.addChild("right_sleeve").box({40, 32}, {slim ? -2.0f : -3.0f, -2, -2}, {armW, 12, 4}, 0.25f);
    ModelPart& leftLeg = root.addChild("left_leg", {{1.9f, 12, 0}});
    leftLeg.box({16, 48}, {-2, 0, -2}, {4, 12, 4});
    leftLeg.addChild("left_pants").box({0, 48}, {-2, 0, -2}, {4, 12, 4}, 0.25f);
    ModelPart& rightLeg = root.addChild("right_leg", {{-1.9f, 12, 0}});
    rightLeg.box({0, 16}, {-2, 0, -2}, {4, 12, 4});
    rightLeg.addChild("right_pants").box({0, 32}, {-2, 0, -2}, {4, 12, 4}, 0.25f);
    return d;
}

ModelDefinition shieldModel() {
    ModelDefinition d;
    d.root.addChild("plate").box({0, 0}, {-6, -11, -2}, {12, 22, 1});
    d.root.addChild("handle").box({26, 0}, {-1, -3, -1}, {2, 6, 6});
    return d;
}

ModelDefinition tridentModel() {
    ModelDefinition d;
    d.texWidth = d.texHeight = 32;
    ModelPart& pole = d.root.addChild("pole");
    pole.box({0, 6}, {-0.5f, 2, -0.5f}, {1, 25, 1});
    pole.addChild("base").box({4, 0}, {-1.5f, 0, -0.5f}, {3, 2, 1});
    pole.addChild("left_spike").box({4, 3}, {-2.5f, -3, -0.5f}, {1, 4, 1});
    pole.addChild("middle_spike").box({0, 0}, {-0.5f, -4, -0.5f}, {1, 4, 1});
    pole.addChild("right_spike").box({4, 3}, {1.5f, -3, -0.5f}, {1, 4, 1}, 0.0f, true);
    return d;
}

} // namespace mc
