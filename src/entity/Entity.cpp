#include "entity/Entity.h"

#include <cmath>

#include "entity/Physics.h"

namespace mc {

AABB Entity::box() const { return boxAt(pos); }

AABB Entity::boxAt(const glm::dvec3& feet) const {
    const double hw = width / 2.0;
    return AABB(feet.x - hw, feet.y, feet.z - hw, feet.x + hw, feet.y + height, feet.z + hw);
}

BlockPos Entity::blockBelow() const {
    return BlockPos(int(std::floor(pos.x)), int(std::floor(pos.y - 0.500001)), int(std::floor(pos.z)));
}

void Entity::move(const BlockGetter& level, glm::dvec3 delta) {
    if (noPhysics) {
        pos += delta;
        horizontalCollision = verticalCollision = verticalCollisionBelow = onGround = false;
        return;
    }
    if (glm::dot(stuck_, stuck_) > 1e-7) {
        delta *= stuck_;
        stuck_ = glm::dvec3(0.0);
        velocity = glm::dvec3(0.0);
    }
    const glm::dvec3 moved = collideWithStep(level, box(), delta, stepHeight, onGround);
    pos += moved;
    const bool xBlocked = std::abs(delta.x - moved.x) > 1e-5, zBlocked = std::abs(delta.z - moved.z) > 1e-5;
    horizontalCollision = xBlocked || zBlocked;
    verticalCollision = delta.y != moved.y;
    verticalCollisionBelow = verticalCollision && delta.y < 0.0;
    onGround = verticalCollisionBelow;
    // Falling distance accumulates while airborne (fall damage arrives with survival).
    if (onGround) fallDistance = 0.0f;
    else if (moved.y < 0.0) fallDistance -= float(moved.y);
    if (xBlocked) velocity.x = 0.0;
    if (zBlocked) velocity.z = 0.0;
    if (verticalCollision) velocity.y = 0.0;
    const Block& below = level.registry().blockOf(level.getBlock(blockBelow()));
    float speed = below.speedFactor;
    if (speed == 1.0f) {
        const Block& at = level.registry().blockOf(
            level.getBlock(BlockPos(int(std::floor(pos.x)), int(std::floor(pos.y)), int(std::floor(pos.z)))));
        speed = at.speedFactor;
    }
    velocity.x *= speed;
    velocity.z *= speed;
}

void Entity::updateFluidState(const BlockGetter& level) {
    const AABB b = box().inflated(-0.001, -0.001, -0.001);
    double water = 0.0, lava = 0.0;
    for (int y = int(std::floor(b.min.y)); y <= int(std::floor(b.max.y)); ++y)
        for (int z = int(std::floor(b.min.z)); z <= int(std::floor(b.max.z)); ++z)
            for (int x = int(std::floor(b.min.x)); x <= int(std::floor(b.max.x)); ++x) {
                const FluidState f = level.fluid({x, y, z});
                if (f.empty()) continue;
                // Fluid surface: full under the same fluid, else (8 - level) / 9 of the block.
                const FluidState above = level.fluid({x, y + 1, z});
                const double top = y + (above.kind == f.kind ? 1.0 : double(f.falling ? 8 : 8 - f.level) / 9.0);
                if (top < b.min.y) continue;
                const double depth = top - b.min.y;
                if (f.kind == FluidKind::Water) water = std::max(water, depth);
                else lava = std::max(lava, depth);
            }
    waterHeight = water;
    lavaHeight = lava;
    inWater = water > 0.0;
    inLava = lava > 0.0;
    if (inWater) fallDistance = 0.0f;
}

} // namespace mc
