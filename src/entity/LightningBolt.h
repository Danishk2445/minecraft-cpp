// LightningBolt.h -- a lightning strike (vanilla LightningBolt): it lights fires where it
// hits (Normal and Hard difficulty), hurts and ignites what stands near, flashes the sky,
// and may strike again once or twice before it fades.
#pragma once

#include <cstdint>

#include "entity/Entity.h"

namespace mc {

class LightningBolt : public Entity {
public:
    LightningBolt();
    std::string_view type() const override { return "minecraft:lightning_bolt"; }
    void tick(Level& level) override;
    std::unique_ptr<Entity> clone() const override { return std::make_unique<LightningBolt>(*this); }

    // Strikes at `pos` (the bottom center of the block); `visualOnly` bolts do no harm.
    static LightningBolt& strike(Level& level, const BlockPos& pos, bool visualOnly);

    int life = 2;     // ticks until it fades (it may flash back to 1)
    int flashes = 1;  // strikes left after the first
    int64_t seed = 0; // the bolt's shape
    bool visualOnly = false;

private:
    void spawnFire(Level& level, int extraIgnitions);
};

} // namespace mc
