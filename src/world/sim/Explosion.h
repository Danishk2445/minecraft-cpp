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

// Explodes with `power` (TNT 4, creepers 3) at `center`; `source` (the TNT, the creeper) is
// neither hurt nor pushed by it, and a living source is blamed for the damage. Without
// `breakBlocks` (mob griefing off) only entities feel it; with `decay` (mobs' blasts) each
// broken block's drops survive one time in `power`. Emits an Explosion level event.
void explode(Level& level, const Entity* source, const glm::dvec3& center, float power, bool breakBlocks = true,
             bool decay = false);

// Vanilla Explosion.getSeenPercent: the fraction of a grid of points on `box` with a clear
// line (through collision shapes) to `center`.
float seenPercent(const BlockGetter& level, const glm::dvec3& center, const AABB& box);

} // namespace sim
} // namespace mc
