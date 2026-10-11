// Item.h -- items: one per item definition in the resource pack, with names,
// stack sizes and the block each block item places (data/items/items.json).
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "world/block/BlockRegistry.h"

namespace mc {

class Language;
class ResourcePack;

// Where an item can be worn (vanilla EquipmentSlot); MainHand for everything else.
enum class EquipmentSlot : uint8_t { MainHand, OffHand, Feet, Legs, Chest, Head };
// Name colors: common white, uncommon yellow, rare aqua, epic light purple.
enum class Rarity : uint8_t { Common, Uncommon, Rare, Epic };

struct Item {
    std::string id; // "minecraft:oak_planks"
    uint16_t index = 0;
    int maxStack = 64;
    const Block* block = nullptr; // the block it places, if any
    std::string nameKey;          // "block.minecraft.oak_planks" or "item.minecraft.diamond"
    EquipmentSlot equipment = EquipmentSlot::MainHand;
    Rarity rarity = Rarity::Common;
};

// The § color code of a rarity ('f', 'e', 'b' or 'd').
char rarityColorCode(Rarity r);

class ItemRegistry {
public:
    bool load(const ResourcePack& pack, const BlockRegistry& blocks, const std::filesystem::path& dataDir,
              std::string* error);

    size_t size() const { return items_.size(); }
    const Item& get(size_t i) const { return items_[i]; }
    const Item* find(std::string_view id) const; // namespace optional
    // The item a block is picked as (wall torch -> torch, wheat crop -> wheat seeds).
    const Item* forBlock(const Block& block) const;
    std::string name(const Item& item, const Language& lang) const;

private:
    std::vector<Item> items_;
    std::unordered_map<std::string, uint16_t> index_;
    std::vector<int> byBlock_; // block index -> item index (-1: none)
};

} // namespace mc
