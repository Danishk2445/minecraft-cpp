// Entity.h -- position, motion and vanilla Entity.move against blocks.
#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "core/Math.h"
#include "world/block/BlockGetter.h"

namespace mc {

class Entity {
public:
    virtual ~Entity() = default;

    glm::dvec3 pos{0.0};      // feet (bottom center of the bounding box)
    glm::dvec3 oldPos{0.0};   // last tick, for interpolation
    glm::dvec3 velocity{0.0}; // blocks per tick
    float yaw = 0.0f, pitch = 0.0f, oldYaw = 0.0f, oldPitch = 0.0f; // degrees; yaw 0 faces +z
    float width = 0.6f, height = 1.8f;
    float stepHeight = 0.0f;
    bool noPhysics = false;

    bool onGround = false;
    bool horizontalCollision = false, verticalCollision = false, verticalCollisionBelow = false;
    float fallDistance = 0.0f;
    // Fluid contact, refreshed by updateFluidState().
    bool inWater = false, inLava = false;
    double waterHeight = 0.0, lavaHeight = 0.0; // how deep the box reaches into each fluid

    // Unit vector of the look direction (vanilla calculateViewVector).
    glm::dvec3 viewVector() const;
    AABB box() const;
    AABB boxAt(const glm::dvec3& feet) const;
    // Vanilla Entity.move(SELF): collide, step, set collision flags, zero blocked
    // velocity components and apply the speed factor of the block underneath.
    void move(const BlockGetter& level, glm::dvec3 delta);
    // Vanilla updateInWaterStateAndDoFluidPushing (without pushing): fluid heights.
    void updateFluidState(const BlockGetter& level);
    // The block that decides friction/speed: just below the feet (vanilla 0.500001 down).
    BlockPos blockBelow() const;
    // Vanilla stuck-in-block slowdown (cobweb, berry bushes, powder snow), consumed by move().
    void makeStuckInBlock(const glm::dvec3& multiplier) { stuck_ = multiplier; }

protected:
    glm::dvec3 stuck_{0.0};
};

} // namespace mc
