#include "game/Item.h"

#include <algorithm>
#include <format>

#include "core/Json.h"
#include "resources/Language.h"
#include "resources/ResourcePack.h"

namespace mc {

bool ItemRegistry::load(const ResourcePack& pack, const BlockRegistry& blocks, const std::filesystem::path& dataDir,
                        std::string* error) {
    items_.clear();
    index_.clear();
    std::vector<std::string> problems;
    std::string err;
    auto data = loadJsonFile(dataDir / "items" / "items.json", &err);
    if (!data || !data->is_object()) {
        if (error) *error = err.empty() ? "items.json: expected an object" : err;
        return false;
    }
    const Json blockItems = data->value("block_items", Json::object());
    const Json notBlocks = data->value("not_block_items", Json::array());
    const Json stackRules = data->value("stack_rules", Json::array());
    const Json equipmentRules = data->value("equipment_rules", Json::array());
    const Json rarityRules = data->value("rarity_rules", Json::array());
    auto matches = [](const Json& rule, const std::string& id) {
        for (const Json& pattern : rule.value("match", Json::array()))
            if (globMatch(pattern.get<std::string>(), id)) return true;
        return false;
    };

    // Every item definition in the pack, sorted for stable indices.
    std::vector<std::string> ids;
    std::error_code ec;
    const std::filesystem::path dir = pack.file("minecraft", "items");
    for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::end(it); it.increment(ec))
        if (it->path().extension() == ".json") ids.push_back(it->path().stem().string());
    std::sort(ids.begin(), ids.end());
    if (ids.empty()) problems.push_back("no item definitions in " + dir.string());

    for (const std::string& id : ids) {
        Item item;
        item.id = "minecraft:" + id;
        item.index = uint16_t(items_.size());
        if (auto m = blockItems.find(id); m != blockItems.end()) {
            item.block = blocks.find(m->get<std::string>());
            if (!item.block) problems.push_back(std::format("items.json: {} places unknown block {}", id, m->dump()));
        } else if (std::find(notBlocks.begin(), notBlocks.end(), id) == notBlocks.end()) {
            item.block = blocks.find(id);
        }
        item.nameKey = (item.block && item.block->id == item.id ? "block.minecraft." : "item.minecraft.") + id;
        for (const Json& rule : stackRules)
            if (matches(rule, id)) {
                item.maxStack = rule.value("stack", 64);
                break;
            }
        for (const Json& rule : equipmentRules)
            if (matches(rule, id)) {
                const std::string slot = rule.value("slot", std::string());
                item.equipment = slot == "head"      ? EquipmentSlot::Head
                                 : slot == "chest"   ? EquipmentSlot::Chest
                                 : slot == "legs"    ? EquipmentSlot::Legs
                                 : slot == "feet"    ? EquipmentSlot::Feet
                                 : slot == "offhand" ? EquipmentSlot::OffHand
                                                     : EquipmentSlot::MainHand;
                break;
            }
        for (const Json& rule : rarityRules)
            if (matches(rule, id)) {
                const std::string r = rule.value("rarity", std::string());
                item.rarity = r == "epic" ? Rarity::Epic : r == "rare" ? Rarity::Rare : r == "uncommon" ? Rarity::Uncommon : Rarity::Common;
                break;
            }
        index_.emplace(item.id, item.index);
        items_.push_back(std::move(item));
    }

    loadStats(*data, problems);
    // Item tags: recipes and fuel use them.
    if (std::filesystem::is_directory(dataDir / "tags" / "items", ec)) {
        std::string tagErr;
        if (!tags_.load(*this, dataDir / "tags" / "items", &tagErr)) problems.push_back(tagErr);
    }
    loadFuel(dataDir / "items" / "fuel.json", problems);

    byBlock_.assign(blocks.blockCount(), -1);
    for (const Item& item : items_)
        if (item.block && byBlock_[item.block->index] < 0) byBlock_[item.block->index] = item.index;
    // Blocks without their own item pick as their floor variant (wall_torch -> torch).
    for (size_t b = 0; b < blocks.blockCount(); ++b) {
        if (byBlock_[b] >= 0) continue;
        std::string id(shortId(blocks.block(b).id));
        if (const size_t w = id.find("wall_"); w != std::string::npos) {
            id.erase(w, 5);
            if (const Item* i = find(id)) byBlock_[b] = i->index;
        }
    }
    if (error) {
        error->clear();
        for (const auto& p : problems) *error += p + "\n";
    }
    return problems.empty();
}

void ItemRegistry::loadStats(const Json& data, std::vector<std::string>& problems) {
    auto number = [](const Json& j, const char* key, float fallback) {
        auto it = j.find(key);
        return it != j.end() && it->is_number() ? it->get<float>() : fallback;
    };
    // Tools: <tier>_<kind>, e.g. diamond_pickaxe.
    const Json tools = data.value("tools", Json::object());
    const Json tiers = tools.value("tiers", Json::object()), kinds = tools.value("kinds", Json::object());
    static constexpr std::pair<const char*, ToolKind> kKinds[] = {{"sword", ToolKind::Sword},   {"pickaxe", ToolKind::Pickaxe},
                                                                  {"shovel", ToolKind::Shovel}, {"axe", ToolKind::Axe},
                                                                  {"hoe", ToolKind::Hoe}};
    for (const auto& [tierName, tier] : tiers.items())
        for (const auto& [kindName, kindValue] : kKinds) {
            Item* item = mutableItem(tierName + "_" + kindName);
            if (!item) {
                problems.push_back(std::format("items.json: no item {}_{}", tierName, kindName));
                continue;
            }
            const Json kind = kinds.value(kindName, Json::object());
            // A kind's modifier is one number, or one per tier.
            auto modifier = [&](const char* key) {
                const Json v = kind.value(key, Json());
                return v.is_object() ? number(v, tierName.c_str(), 0.0f) : v.is_number() ? v.get<float>() : 0.0f;
            };
            item->tool = kindValue;
            item->toolLevel = tier.value("level", 0);
            item->toolSpeed = number(tier, "speed", 1.0f);
            item->maxDamage = tier.value("durability", 0);
            item->attackDamage = 1.0f + modifier("damage") + number(tier, "damage", 0.0f);
            item->attackSpeed = 4.0f + modifier("speed");
            item->attackModifiers = true;
        }
    if (Item* shears = mutableItem("shears")) shears->tool = ToolKind::Shears;
    // Armor: <material>_<piece>.
    const Json armor = data.value("armor", Json::object());
    const Json multipliers = armor.value("multipliers", Json::object());
    static constexpr const char* kPieces[] = {"boots", "leggings", "chestplate", "helmet"};
    const Json materials = armor.value("materials", Json::object());
    for (const auto& [materialName, material] : materials.items())
        for (int piece = 0; piece < 4; ++piece) {
            Item* item = mutableItem(materialName + "_" + kPieces[piece]);
            if (!item) continue; // the turtle shell is only a helmet
            const Json defense = material.value("defense", Json::array());
            item->armor = piece < int(defense.size()) ? defense[size_t(piece)].get<int>() : 0;
            item->toughness = number(material, "toughness", 0.0f);
            item->knockbackResistance = number(material, "knockback_resistance", 0.0f);
            item->maxDamage = material.value("durability", 0) * multipliers.value(kPieces[piece], 0);
        }
    const Json durabilities = data.value("durability", Json::object()), foods = data.value("food", Json::object());
    for (const auto& [id, durability] : durabilities.items()) {
        if (Item* item = mutableItem(id)) item->maxDamage = durability.get<int>();
        else problems.push_back("items.json: durability for unknown item " + id);
    }
    for (const auto& [id, food] : foods.items()) {
        Item* item = mutableItem(id);
        if (!item) {
            problems.push_back("items.json: food for unknown item " + id);
            continue;
        }
        item->nutrition = food.value("nutrition", 0);
        item->saturation = number(food, "saturation", 0.0f);
        item->alwaysEdible = food.value("always", false);
        item->useTicks = food.value("ticks", 32);
        if (food.contains("remainder")) {
            item->remainder = find(food.value("remainder", std::string()));
            if (!item->remainder) problems.push_back("items.json: unknown remainder for " + id);
        }
    }
}

void ItemRegistry::loadFuel(const std::filesystem::path& file, std::vector<std::string>& problems) {
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) return;
    std::string err;
    auto doc = loadJsonFile(file, &err);
    if (!doc || !doc->is_object()) {
        problems.push_back(err.empty() ? "fuel.json: expected an object" : err);
        return;
    }
    const Json rules = doc->value("rules", Json::array());
    for (Item& item : items_) {
        const std::string_view id = shortId(item.id);
        for (const Json& rule : rules) {
            bool hit = false;
            for (const Json& m : rule.value("match", Json::array())) {
                const std::string entry = m.get<std::string>();
                hit = entry.starts_with('#') ? inTag(item, entry) : globMatch(entry, id);
                if (hit) break;
            }
            if (hit) {
                item.fuelTicks = rule.value("ticks", 0);
                break;
            }
        }
    }
}

bool ItemRegistry::inTag(const Item& item, std::string_view tag) const {
    const auto t = tags_.find(tag);
    return t && tags_.containsItem(*t, item);
}

bool ItemTags::load(const ItemRegistry& items, const std::filesystem::path& dir, std::string* error) {
    return TagSet::load(
        dir, items.size(), [&](size_t i) -> std::string_view { return items.get(i).id; }, "item", error);
}

char rarityColorCode(Rarity r) {
    switch (r) {
    case Rarity::Uncommon: return 'e';
    case Rarity::Rare: return 'b';
    case Rarity::Epic: return 'd';
    default: return 'f';
    }
}

Item* ItemRegistry::mutableItem(std::string_view id) {
    std::string key(id);
    if (!key.starts_with("minecraft:")) key = "minecraft:" + key;
    auto it = index_.find(key);
    return it == index_.end() ? nullptr : &items_[it->second];
}

const Item* ItemRegistry::find(std::string_view id) const {
    std::string key(id);
    if (!key.starts_with("minecraft:")) key = "minecraft:" + key;
    auto it = index_.find(key);
    return it == index_.end() ? nullptr : &items_[it->second];
}

const Item* ItemRegistry::forBlock(const Block& block) const {
    const int i = block.index < byBlock_.size() ? byBlock_[block.index] : -1;
    return i < 0 ? nullptr : &items_[size_t(i)];
}

std::string ItemRegistry::name(const Item& item, const Language& lang) const {
    if (lang.has(item.nameKey)) return lang.get(item.nameKey);
    // Block items whose block id differs (redstone -> redstone_wire) use the item key.
    const std::string itemKey = "item.minecraft." + std::string(shortId(item.id));
    return lang.has(itemKey) ? lang.get(itemKey) : std::string(shortId(item.id));
}

} // namespace mc
