// Explosion.h -- explosions (vanilla ServerExplosion): rays from the center lose
// strength to each block's blast resistance and break what they get through; entities
// within twice the power are hurt and thrown by how much of them sees the center.
#pragma once

#include <glm/glm.hpp>

#include "core/Math.h"

namespace mc {

class BlockGetter;
class Entity;
class Level;

namespace sim {

// Explodes with `power` (TNT 4) at `center`; `source` (the TNT) is neither hurt nor
// pushed by it. Emits an Explosion level event.
void explode(Level& level, const Entity* source, const glm::dvec3& center, float power);

// Vanilla Explosion.getSeenPercent: the fraction of a grid of points on `box` with a clear
// line (through collision shapes) to `center`.
float seenPercent(const BlockGetter& level, const glm::dvec3& center, const AABB& box);

} // namespace sim
} // namespace mc
