// Raycast.h -- the block a ray hits first (vanilla BlockGetter.clip with OUTLINE shapes).
#pragma once

#include <optional>

#include "core/Math.h"
#include "world/block/BlockGetter.h"

namespace mc {

struct BlockHit {
    BlockPos pos;
    Direction face = Direction::Up; // the side that was hit
    glm::dvec3 point{0.0};          // where the ray entered the shape
    double distance = 0.0;
};

// Ray-box entry: the distance along `dir` (within [0, maxT]) and the face entered.
std::optional<std::pair<double, Direction>> clipBox(const AABB& box, const glm::dvec3& from, const glm::dvec3& dir,
                                                     double maxT);

// The first block outline along from -> from + dir * length (dir normalized).
// With `fluids`, fluid sources and flows count as full blocks of their height.
std::optional<BlockHit> raycastBlocks(const BlockGetter& level, const glm::dvec3& from, const glm::dvec3& dir,
                                      double length, bool fluids = false);

} // namespace mc
