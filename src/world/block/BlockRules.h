// BlockRules.h -- block behavior that needs the world around a block. For now:
// vanilla canSurvive for the blocks world generation places (plants on dirt,
// sugar cane by water, cactus on sand, water plants, vines, cave plants,
// dripstone, snow). Player placement rules extend it in M6.
#pragma once

#include <vector>

#include "world/block/BlockGetter.h"

namespace mc {

class BlockRules {
public:
    explicit BlockRules(const BlockRegistry& registry);

    // Vanilla BlockState.canSurvive at `pos` (the block at `pos` itself is ignored).
    bool canSurvive(const BlockGetter& level, StateId state, const BlockPos& pos) const;
    // Vanilla Block.isFaceFull-style support from the block below for snow layers.
    bool snowCanSurvive(const BlockGetter& level, const BlockPos& pos) const;
    // canSurvive reads only the block's own column (plants, dripstone, carpets...),
    // not horizontal neighbors (vines, cocoa, cactus, wall-mounted blocks).
    bool supportedFromColumn(StateId state) const;

private:
    enum class Kind : uint8_t {
        Always, Plant, DryPlant, Mushroom, SugarCane, Cactus, LilyPad, Seagrass, Kelp, Coral, WallCoral,
        SeaPickle, Bamboo, Cocoa, Vine, Multiface, Snow, CaveVines, HangingFromCeiling, SporeBlossom, SmallDripleaf,
        BigDripleaf, BigDripleafStem, PointedDripstone, DoublePlantTop, Carpet, Azalea, LeafLitter, Farmland,
        CactusFlower
    };
    const BlockRegistry& reg_;
    std::vector<Kind> kind_; // per block index
    TagId dirt_ = 0, sand_ = 0, dry_ = 0, mushroomGrow_ = 0, bambooOn_ = 0, snowNever_ = 0, snowAlways_ = 0, azalea_ = 0,
          smallDripleafOn_ = 0, bigDripleafOn_ = 0, jungleLogs_ = 0;
    uint16_t farmland_ = 0, sugarCane_ = 0, cactus_ = 0, ice_ = 0, frostedIce_ = 0, magma_ = 0, kelp_ = 0, kelpPlant_ = 0,
             caveVines_ = 0, caveVinesPlant_ = 0, bigDripleaf_ = 0, bigDripleafStem_ = 0, smallDripleaf_ = 0,
             pointedDripstone_ = 0, tallSeagrass_ = 0, snow_ = 0, bamboo_ = 0, clay_ = 0;
};

} // namespace mc
