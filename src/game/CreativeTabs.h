// CreativeTabs.h -- the creative inventory's tabs (data/creative_tabs.json,
// vanilla CreativeModeTabs order) and its search (vanilla's plain-text search:
// a case-insensitive substring of the item's name or id).
#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace mc {

struct Item;
class ItemRegistry;

struct CreativeTab {
    std::string id;      // "building_blocks"
    std::string nameKey; // "itemGroup.buildingBlocks"
    const Item* icon = nullptr;
    bool top = true; // row
    int column = 0;
    bool hidden = false; // operator utilities
    std::vector<const Item*> items;
};

class CreativeTabs {
public:
    bool load(const std::filesystem::path& dataDir, const ItemRegistry& items, std::string* error);
    const std::vector<CreativeTab>& tabs() const { return tabs_; }
    // Every item of the visible tabs in tab order, each once: the search tab's contents.
    const std::vector<const Item*>& searchable() const { return searchable_; }
    // The searchable items whose name or id contains `query` (case-insensitive).
    std::vector<const Item*> search(std::string_view query,
                                    const std::function<std::string(const Item&)>& displayName) const;

private:
    std::vector<CreativeTab> tabs_;
    std::vector<const Item*> searchable_;
};

} // namespace mc
