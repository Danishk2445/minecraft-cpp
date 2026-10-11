// Physics.h -- vanilla entity collision against block shapes.
#pragma once

#include <vector>

#include "core/Math.h"
#include "world/block/BlockGetter.h"

namespace mc {

// Vanilla Shapes.collide along one axis: how far `box` may move by `d` before
// touching any of `shapes` (world-space boxes).
double collideAxis(int axis, const AABB& box, const std::vector<AABB>& shapes, double d);

// World-space collision boxes of the blocks overlapping `region`.
void gatherBlockCollisions(const BlockGetter& level, const AABB& region, std::vector<AABB>& out);

// Vanilla Entity.collideBoundingBox (blocks only): Y first, then the larger
// horizontal axis, then the other.
glm::dvec3 collideBoundingBox(const BlockGetter& level, const AABB& box, const glm::dvec3& delta);

// Vanilla Entity.collide: collideBoundingBox plus stepping up to `stepHeight`
// when moving into an obstacle on (or onto) the ground.
glm::dvec3 collideWithStep(const BlockGetter& level, const AABB& box, const glm::dvec3& delta, double stepHeight,
                           bool onGround);

// True if `box` overlaps no block collision shape.
bool noCollision(const BlockGetter& level, const AABB& box);

} // namespace mc
