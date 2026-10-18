// DisplayTicks.h -- the client's ambient particles (vanilla ClientLevel.animateTick and
// the blocks' and fluids' animateTick): each tick 667 random blocks within 16 of the
// camera and 667 within 32 may show theirs (torch flames, fire smoke, lava pops,
// underwater motes, drips under fluids, falling leaves and rain dripping off them, lit
// furnaces and candles); and the rain's splashes on the ground (WeatherEffectRenderer.
// tickRainParticles).
#pragma once

#include <functional>

#include "core/Math.h"
#include "world/block/BlockGetter.h"

namespace mc {

class Level;
class ParticleSystem;

// The tint of a block at a position (falling leaves take their leaves' color), 0xRRGGBB.
using TintAt = std::function<uint32_t(const BlockPos&, StateId)>;

void animateTick(const Level& level, const BlockPos& center, ParticleSystem& particles, const TintAt& tint);
void tickRainParticles(const Level& level, const BlockPos& center, int64_t ticks, ParticleSystem& particles);

} // namespace mc
