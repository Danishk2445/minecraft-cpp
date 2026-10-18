#include "entity/PrimedTnt.h"

#include <cmath>

#include "world/level/Level.h"
#include "world/sim/Explosion.h"

namespace mc {

PrimedTnt::PrimedTnt() { width = height = 0.98f; }

PrimedTnt& PrimedTnt::spawn(Level& level, const BlockPos& pos) {
    auto e = std::make_unique<PrimedTnt>();
    e->pos = e->oldPos = glm::dvec3(pos.x + 0.5, pos.y, pos.z + 0.5);
    // A little hop in a random direction.
    const double a = level.random().nextDouble() * 6.2831855;
    e->velocity = {-std::sin(a) * 0.02, 0.2f, -std::cos(a) * 0.02};
    return static_cast<PrimedTnt&>(level.addEntity(std::move(e)));
}

PrimedTnt& PrimedTnt::prime(Level& level, const BlockPos& pos) { return spawn(level, pos); }

void PrimedTnt::primeByExplosion(Level& level, const BlockPos& pos) {
    PrimedTnt& tnt = spawn(level, pos);
    tnt.fuse = level.random().nextInt(kFuse / 4) + kFuse / 8;
}

void PrimedTnt::tick(Level& level) {
    ++tickCount;
    oldPos = pos;
    velocity.y -= 0.04; // gravity
    move(level, velocity);
    velocity *= 0.98;
    if (onGround) velocity *= glm::dvec3(0.7, -0.5, 0.7);
    if (--fuse <= 0) {
        removed = true;
        // Exploding from 1/16 of its height, power 4, breaking blocks.
        sim::explode(level, this, pos + glm::dvec3(0.0, double(height) * 0.0625, 0.0), 4.0f);
        return;
    }
    updateFluidState(level);
}

void PrimedTnt::saveExtra(Json& out) const { out["fuse"] = fuse; }

void PrimedTnt::loadExtra(const Json& in, const ItemRegistry&) { fuse = in.value("fuse", kFuse); }

} // namespace mc
