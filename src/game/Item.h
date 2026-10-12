// Item.h -- items: one per item definition in the resource pack, with names,
// stack sizes and the block each block item places (data/items/items.json).
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/Json.h"
#include "core/TagSet.h"
#include "world/block/BlockRegistry.h"

namespace mc {

class Language;
class ResourcePack;

// Where an item can be worn (vanilla EquipmentSlot); MainHand for everything else.
enum class EquipmentSlot : uint8_t { MainHand, OffHand, Feet, Legs, Chest, Head };
// Name colors: common white, uncommon yellow, rare aqua, epic light purple.
enum class Rarity : uint8_t { Common, Uncommon, Rare, Epic };
// What a tool is for (vanilla's Tool component rules per item kind).
enum class ToolKind : uint8_t { None, Pickaxe, Axe, Shovel, Hoe, Sword, Shears };

struct Item {
    std::string id; // "minecraft:oak_planks"
    uint16_t index = 0;
    int maxStack = 64;
    const Block* block = nullptr; // the block it places, if any
    std::string nameKey;          // "block.minecraft.oak_planks" or "item.minecraft.diamond"
    EquipmentSlot equipment = EquipmentSlot::MainHand;
    Rarity rarity = Rarity::Common;
    int maxDamage = 0; // durability; 0 = never wears out

    // Tools: the kind, the tier's level (0 wood and gold, 1 stone, 2 iron, 3 diamond,
    // 4 netherite) and its mining speed on the blocks the tool is for.
    ToolKind tool = ToolKind::None;
    int toolLevel = 0;
    float toolSpeed = 1.0f;
    // In the main hand: attack damage and attacks per second (the player's base 1 and 4
    // plus the item's modifiers), shown in tooltips when the item has modifiers.
    float attackDamage = 1.0f, attackSpeed = 4.0f;
    bool attackModifiers = false;
    // Worn: armor points, toughness and knockback resistance.
    int armor = 0;
    float toughness = 0.0f, knockbackResistance = 0.0f;
    // Food: hunger and saturation restored; always edible; ticks to eat; what is left.
    int nutrition = 0;
    float saturation = 0.0f;
    bool alwaysEdible = false;
    int useTicks = 32;
    const Item* remainder = nullptr;
    // Furnace fuel: ticks it burns (0 = not a fuel).
    int fuelTicks = 0;

    bool isFood() const { return nutrition > 0 || alwaysEdible; }
};

// The § color code of a rarity ('f', 'e', 'b' or 'd').
char rarityColorCode(Rarity r);

// Named item sets from data/tags/items/**.json (core/TagSet's format).
class ItemTags : public TagSet {
public:
    bool load(const class ItemRegistry& items, const std::filesystem::path& dir, std::string* error);
    bool containsItem(TagId t, const Item& item) const { return contains(t, item.index); }
};

class ItemRegistry {
public:
    // Items, their stats (data/items/items.json), item tags and fuel (data/items/fuel.json).
    bool load(const ResourcePack& pack, const BlockRegistry& blocks, const std::filesystem::path& dataDir,
              std::string* error);

    size_t size() const { return items_.size(); }
    const Item& get(size_t i) const { return items_[i]; }
    const Item* find(std::string_view id) const; // namespace optional
    // The item a block is picked as (wall torch -> torch, wheat crop -> wheat seeds).
    const Item* forBlock(const Block& block) const;
    std::string name(const Item& item, const Language& lang) const;
    const ItemTags& tags() const { return tags_; }
    // Whether an item is in a tag ("planks", "#minecraft:planks"); false for unknown tags.
    bool inTag(const Item& item, std::string_view tag) const;

private:
    void loadStats(const Json& data, std::vector<std::string>& problems);
    Item* mutableItem(std::string_view id);
    void loadFuel(const std::filesystem::path& file, std::vector<std::string>& problems);

    std::vector<Item> items_;
    ItemTags tags_;
    std::unordered_map<std::string, uint16_t> index_;
    std::vector<int> byBlock_; // block index -> item index (-1: none)
};

} // namespace mc
