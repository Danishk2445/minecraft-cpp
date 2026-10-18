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

// Which shapes a ray stops at: OUTLINE targets (what the crosshair selects),
// VISUAL ones (what blocks the camera: collision shapes, except that glass and
// panes let it through), or COLLIDER ones (every collision shape: explosions).
enum class RayShape : uint8_t { Outline, Visual, Collider };

// Which fluids a ray stops at (vanilla ClipContext.Fluid): none, sources only (buckets),
// or any; a fluid counts as a box of its height.
enum class RayFluid : uint8_t { None, SourceOnly, Any };

// The first block shape (or fluid) along from -> from + dir * length (dir normalized).
std::optional<BlockHit> raycastBlocks(const BlockGetter& level, const glm::dvec3& from, const glm::dvec3& dir,
                                      double length, RayFluid fluids = RayFluid::None,
                                      RayShape shape = RayShape::Outline);

// Vanilla Camera.getMaxZoom: how far (at most `maxZoom`) a third-person camera can
// back away from `eye` against `forward` before one of 8 rays from the corners of a
// 0.2-block cube around the eye hits a block.
double cameraDistance(const BlockGetter& level, const glm::dvec3& eye, const glm::dvec3& forward, double maxZoom);

} // namespace mc
