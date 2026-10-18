#include "entity/Entity.h"

#include <cmath>
#include <limits>

#include "entity/Physics.h"
#include "world/sim/Fluids.h"

namespace mc {

glm::dvec3 Entity::viewVector() const {
    const double y = glm::radians(double(yaw)), p = glm::radians(double(pitch));
    return {-std::sin(y) * std::cos(p), -std::sin(p), std::cos(y) * std::cos(p)};
}

AABB Entity::box() const { return boxAt(pos); }

AABB Entity::boxAt(const glm::dvec3& feet) const {
    const double hw = width / 2.0;
    return AABB(feet.x - hw, feet.y, feet.z - hw, feet.x + hw, feet.y + height, feet.z + hw);
}

bool Entity::insideBlocks(const BlockGetter& level) const {
    return !noCollision(level, box().inflated(-1e-7, -1e-7, -1e-7));
}

void Entity::moveTowardsClosestSpace(const BlockGetter& level, float random01) {
    const AABB b = box();
    const glm::dvec3 center((b.min.x + b.max.x) / 2.0, (b.min.y + b.max.y) / 2.0, (b.min.z + b.max.z) / 2.0);
    const BlockPos cell = BlockPos::containing(center);
    const glm::dvec3 local = center - glm::dvec3(cell.x, cell.y, cell.z);
    auto fullBlock = [&](const BlockPos& p) {
        const Shape& shape = level.registry().collisionShape(level.getBlock(p));
        return shape.size() == 1 && shape[0].min == glm::dvec3(0.0) && shape[0].max == glm::dvec3(1.0);
    };
    // North, south, west, east, up: the nearest open side wins, up on ties (it is checked last
    // with < but starts as the default).
    struct Side {
        glm::ivec3 step;
        int axis;
    };
    static constexpr Side kSides[] = {{{0, 0, -1}, 2}, {{0, 0, 1}, 2}, {{-1, 0, 0}, 0}, {{1, 0, 0}, 0}, {{0, 1, 0}, 1}};
    Side best = kSides[4];
    double bestDistance = std::numeric_limits<double>::max();
    for (const Side& side : kSides) {
        if (fullBlock(cell.offset(side.step.x, side.step.y, side.step.z))) continue;
        const int sign = side.step[side.axis];
        const double d = sign > 0 ? 1.0 - local[side.axis] : local[side.axis];
        if (d < bestDistance) {
            bestDistance = d;
            best = side;
        }
    }
    const double push = double(random01 * 0.2f + 0.1f) * double(best.step[best.axis]);
    velocity *= 0.75;
    velocity[best.axis] = push;
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
    // Falling distance accumulates (the landing tick's drop included, as in 1.21.5+); landing
    // records it for fall damage (vanilla checkFallDamage), with the block landed on (0.2
    // below the feet, or under that when it is air, as on a fence).
    if (!inWater && moved.y < 0.0) fallDistance -= double(float(moved.y));
    if (onGround) {
        if (fallDistance > 0.0) {
            landedFall = fallDistance;
            landedOn = BlockPos(int(std::floor(pos.x)), int(std::floor(pos.y - 0.2)), int(std::floor(pos.z)));
            if (level.registry().info(level.getBlock(landedOn)).flags & kAir) landedOn = landedOn.offset(0, -1, 0);
        }
        fallDistance = 0.0;
    }
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
    // Vanilla updateFluidHeightAndDoFluidPushing, once for water and once for lava: the
    // depth, and the cells' currents (each weakened while the depth is under 0.4).
    const AABB b = box().inflated(-0.001, -0.001, -0.001);
    const bool push = pushedByFluid();
    for (const FluidKind kind : {FluidKind::Water, FluidKind::Lava}) {
        double depth = 0.0;
        glm::dvec3 current(0.0);
        int cells = 0;
        for (int x = int(std::floor(b.min.x)); x <= int(std::floor(b.max.x)); ++x)
            for (int y = int(std::floor(b.min.y)); y <= int(std::floor(b.max.y)); ++y)
                for (int z = int(std::floor(b.min.z)); z <= int(std::floor(b.max.z)); ++z) {
                    const FluidState f = level.fluid({x, y, z});
                    if (f.kind != kind) continue;
                    // Fluid surface: full under the same fluid, else (8 - level) / 9 of the block.
                    const FluidState above = level.fluid({x, y + 1, z});
                    const double top = y + (above.kind == f.kind ? 1.0 : double(f.falling ? 8 : 8 - f.level) / 9.0);
                    if (top < b.min.y) continue;
                    depth = std::max(depth, top - b.min.y);
                    if (!push) continue;
                    glm::dvec3 flow = sim::fluidFlow(level, {x, y, z});
                    if (depth < 0.4) flow *= depth;
                    current += flow;
                    ++cells;
                }
        if (glm::length(current) > 0.0) {
            current /= double(cells);
            // Only players feel the current's strength; everything else gets its direction.
            if (!isPlayer()) current = glm::normalize(current);
            current *= kind == FluidKind::Water ? 0.014 : 0.0023333333333333335;
            if (std::abs(velocity.x) < 0.003 && std::abs(velocity.z) < 0.003 && glm::length(current) < 0.0045000000000000005)
                current = glm::normalize(current) * 0.0045000000000000005;
            velocity += current;
        }
        if (kind == FluidKind::Water) waterHeight = depth;
        else lavaHeight = depth;
    }
    inWater = waterHeight > 0.0;
    inLava = lavaHeight > 0.0;
    if (inWater) fallDistance = 0.0;
}

} // namespace mc
