// EntityTypes.h -- the mobs' data (data/entities.json): sizes and attributes per
// type, what they eat, where they spawn (per biome, vanilla MobSpawnSettings), and
// what they drop (vanilla entity loot tables).
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Rng.h"
#include "game/Inventory.h"
#include "world/biome/Biome.h"

namespace mc {

class BiomeRegistry;
class ItemRegistry;

// Vanilla MobCategory: its cap per 289 spawnable chunks, and whether it spawns all the
// time (monsters) or every 400 ticks (creatures).
enum class MobCategory : uint8_t { Monster, Creature, Misc };
int mobCap(MobCategory c);

struct EntityTypeInfo {
    std::string id; // "minecraft:zombie"
    MobCategory category = MobCategory::Misc;
    float width = 0.6f, height = 1.8f;
    float eyeHeight = 0.0f; // 0.85 of the height unless set
    float maxHealth = 20.0f, movementSpeed = 0.25f, followRange = 16.0f, attackDamage = 2.0f, armor = 0.0f;
    bool undead = false;   // breathes under water (vanilla's undead tag)
    std::string food;      // item tag it is bred with and tempted by
};

struct SpawnerData {
    const EntityTypeInfo* type = nullptr;
    int weight = 1, minCount = 1, maxCount = 1;
};

// What a mob died of, for its loot.
struct DeathContext {
    bool killedByPlayer = false; // a player hit it within the last 100 ticks
    bool onFire = false;
    std::string killerType;      // the type that dealt the killing blow ("minecraft:skeleton")
    int woolColor = -1;          // sheep: the color index, -1 when sheared or not a sheep
};

class EntityTypes {
public:
    bool load(const std::filesystem::path& file, const ItemRegistry& items, const BiomeRegistry& biomes,
              std::string* error);

    const EntityTypeInfo* find(std::string_view id) const; // "zombie" or "minecraft:zombie"
    const std::vector<EntityTypeInfo>& all() const { return types_; }
    // The spawn list of a category in a biome, and the chance per generated chunk that
    // animals appear (vanilla creatureGenerationProbability).
    const std::vector<SpawnerData>& spawns(BiomeId biome, MobCategory category) const;
    float creatureProbability(BiomeId biome) const;
    // Rolls the type's loot (vanilla dropFromLootTable).
    std::vector<ItemStack> loot(const EntityTypeInfo& type, const DeathContext& death, Xoroshiro& random) const;

private:
    struct LootEntry {
        std::vector<const Item*> items; // one picked at random
        std::unordered_map<const Item*, const Item*> cooked;
        int min = 1, max = 1;
        float chance = 1.0f;
        bool playerKill = false, wool = false;
        std::string killer;
    };
    std::vector<EntityTypeInfo> types_;
    std::unordered_map<std::string, std::vector<LootEntry>> loot_;
    struct BiomeSpawns {
        std::vector<SpawnerData> creature, monster;
        float creatureProbability = 0.1f;
    };
    std::vector<BiomeSpawns> biomes_; // by BiomeId
    std::vector<const Item*> wool_;   // by color index
    const ItemRegistry* items_ = nullptr;
};

} // namespace mc
