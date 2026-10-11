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
        for (const Json& rule : stackRules) {
            bool hit = false;
            for (const Json& pattern : rule.value("match", Json::array())) hit |= globMatch(pattern.get<std::string>(), id);
            if (hit) {
                item.maxStack = rule.value("stack", 64);
                break;
            }
        }
        index_.emplace(item.id, item.index);
        items_.push_back(std::move(item));
    }

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
