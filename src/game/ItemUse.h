// ItemUse.h -- what using a held item on a block does (vanilla Item.useOn for hoes,
// shovels, axes, shears, flint and steel and honeycomb): the block it becomes, what it
// drops, and the item's wear or use.
#pragma once

#include <optional>
#include <vector>

#include "game/Inventory.h"
#include "world/block/BlockGetter.h"

namespace mc {

struct ItemUseResult {
    BlockPos pos{0, 0, 0}; // the block that changes (the clicked one, or beside it for fire)
    StateId state = 0;     // what it becomes
    std::vector<ItemStack> drops; // popped out from the face (pumpkin seeds, hanging roots)
    int wear = 0;          // durability used
    bool consume = false;  // one of the stack is used up (honeycomb)
};

// The block with `from`'s property values where `to` shares them (stairs keep their facing
// when waxed, logs their axis when stripped).
StateId withSameProperties(const BlockRegistry& registry, StateId from, const Block& to);

// Using `held` on the block at `pos` (clicked on `face`, the player facing `playerFacing`);
// nothing when the item does nothing there.
std::optional<ItemUseResult> useItemOn(const BlockGetter& level, const ItemRegistry& items, const ItemStack& held,
                                       const BlockPos& pos, Direction face, Direction playerFacing);

} // namespace mc
