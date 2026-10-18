// Combat.h -- the player's melee attack (vanilla Player.attack): damage scaled by the
// attack cooldown, critical hits when falling, sprint knockback, sword sweeps, weapon
// wear, hunger; and spawn eggs (vanilla SpawnEggItem).
#pragma once

#include <optional>
#include <string>

#include <glm/glm.hpp>

#include "core/Math.h"

namespace mc {

class Entity;
struct Item;
class Level;
class Mob;
class Player;
struct ItemStack;

struct AttackResult {
    bool hurt = false;     // the target took damage
    bool critical = false;
    bool sweep = false;
    bool knockback = false; // a sprint knockback
    float damage = 0.0f;    // what the hit was worth, before armor
    float dealt = 0.0f;     // the health it took (living targets)
};

// Vanilla Player.attack against `target` with the selected item.
AttackResult playerAttack(Level& level, Player& player, Entity& target);

// The mob type a spawn egg spawns ("minecraft:pig_spawn_egg" -> "minecraft:pig"), if any.
std::optional<std::string> spawnEggType(const Item& item);
// Vanilla SpawnEggItem.useOn: a mob on the clicked block's face (or in it, when it has no
// collision), standing on what's below. Null when it can't.
Mob* spawnFromEgg(Level& level, const Item& egg, const BlockPos& clicked, Direction face);
// Vanilla Mob's spawn egg interaction: an egg of its own kind makes a baby beside it.
Mob* spawnBabyFromEgg(Level& level, Mob& parent, const Item& egg);

} // namespace mc
