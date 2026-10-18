// PrimedTnt.h -- lit TNT (vanilla PrimedTnt): it hops up, falls, and after its fuse
// (80 ticks; shorter when another explosion lit it) explodes with power 4.
#pragma once

#include "entity/Entity.h"

namespace mc {

class PrimedTnt : public Entity {
public:
    static constexpr int kFuse = 80;

    PrimedTnt();
    std::string_view type() const override { return "minecraft:tnt"; }
    void tick(Level& level) override;
    void saveExtra(Json& out) const override;
    void loadExtra(const Json& in, const ItemRegistry& items) override;
    std::unique_ptr<Entity> clone() const override { return std::make_unique<PrimedTnt>(*this); }

    // Vanilla TntBlock.prime: the TNT block at `pos` becomes a lit one (the caller removes
    // the block, or it is already gone).
    static PrimedTnt& prime(Level& level, const BlockPos& pos);
    // Vanilla TntBlock.wasExploded: blown up by another explosion, on a short fuse.
    static void primeByExplosion(Level& level, const BlockPos& pos);

    int fuse = kFuse;

private:
    static PrimedTnt& spawn(Level& level, const BlockPos& pos);
};

} // namespace mc
