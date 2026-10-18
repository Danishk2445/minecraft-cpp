// NaturalSpawner.h -- mobs appearing on their own (vanilla NaturalSpawner): each tick
// a random spot in every chunk near the player tries packs of its biome's mobs,
// within the category caps (70 monsters, 10 animals per 289 chunks); animals also
// fill newly generated chunks. Monsters need the dark, animals grass and light.
#pragma once

#include "core/Math.h"
#include "core/Rng.h"
#include "entity/mob/EntityTypes.h"

namespace mc {

class Level;
class Mob;

namespace spawner {

// One tick of natural spawning (vanilla ServerChunkCache.tickChunks' part): monsters every
// tick when not Peaceful, animals every 400 ticks.
void tick(Level& level);

// Vanilla spawnMobsForChunkGeneration: the animals a freshly generated chunk starts with.
// Returns how many spawned.
int populateChunk(Level& level, ChunkPos chunk);

// Vanilla SpawnPlacements.isSpawnPositionOk (ON_GROUND): a valid floor below, and the
// space and the one above it free of full collision, fluid, rails, redstone and harm.
bool isSpawnPositionOk(const Level& level, const BlockPos& p);
// Vanilla BlockState.isValidSpawn for the floor: a sturdy top, not too bright, and not
// glass, leaves, ice, barriers, bedrock or magma.
bool isValidSpawnFloor(const Level& level, const BlockPos& p);
// Vanilla SpawnPlacements.checkSpawnRules: the type's own rule (animals on lit grass,
// monsters in the dark on a valid floor).
bool checkSpawnRules(const Level& level, const EntityTypeInfo& type, const BlockPos& p, Xoroshiro& random);
// The mobs counting toward each category's cap now, and the caps (by the spawnable
// chunks around the player).
int mobCount(const Level& level, MobCategory category);
int localCap(const Level& level, MobCategory category);

} // namespace spawner
} // namespace mc
