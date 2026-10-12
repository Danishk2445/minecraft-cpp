// Tools.h -- what the held item does to blocks: vanilla 1.21.5+ Tool component rules
// per item kind (pickaxe, axe, shovel, hoe, sword, shears), Player.getDestroySpeed,
// hasCorrectToolForDrops and BlockBehaviour.getDestroyProgress, and the wear of mining.
#pragma once

#include "game/Inventory.h"
#include "world/block/BlockRegistry.h"

namespace mc {

class Player;

// The held item's mining speed on the block (1 when no rule applies).
float toolSpeed(const ItemStack& held, StateId state, const BlockRegistry& registry);
// Whether the held item is the right tool for drops (vanilla isCorrectToolForDrops).
bool correctToolForDrops(const ItemStack& held, StateId state, const BlockRegistry& registry);
// Breaking it drops something: the block needs no tool, or the right one is held.
bool canHarvest(const ItemStack& held, StateId state, const BlockRegistry& registry);
// The fraction of the block broken each tick of mining (vanilla getDestroyProgress):
// the player's speed (slower under water and in the air) / hardness / 30, or / 100 when
// the block needs a tool the player isn't holding. 0 for unbreakable blocks.
float destroyProgressPerTick(StateId state, const Player& player, const BlockRegistry& registry);
// Durability the held item loses for breaking the block (vanilla Item.mineBlock): 1 for
// tools (2 for swords) unless the block breaks instantly; shears wear on anything but fire.
int miningWear(const ItemStack& held, StateId state, const BlockRegistry& registry);

} // namespace mc
