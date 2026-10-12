// Recipes.h -- crafting and cooking recipes from data/recipes (vanilla's 1.21.2+ recipe
// JSON, plus "families": tables of variables crossed with recipe templates).
//
// Crafting matches vanilla CraftingInput: the grid is trimmed to its filled cells; a
// shaped recipe matches as written or mirrored left to right; a shapeless one when its
// ingredients pair off with the items. Two damaged copies of a tool repair each other
// (vanilla RepairItemRecipe). Cooking recipes map one input to a result per appliance.
#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "game/Inventory.h"

namespace mc {

// Any of a set of items (vanilla Ingredient); empty for an empty grid cell.
struct Ingredient {
    std::vector<const Item*> items;
    bool empty() const { return items.empty(); }
    bool test(const ItemStack& s) const;
};

enum class RecipeType : uint8_t { Shaped, Shapeless, Smelting, Blasting, Smoking, CampfireCooking };

struct Recipe {
    RecipeType type = RecipeType::Shaped;
    int width = 0, height = 0;           // shaped
    std::vector<Ingredient> ingredients; // shaped: width x height, row by row; else the list
    ItemStack result;
    float experience = 0.0f; // cooking
    int cookingTime = 200;   // cooking, ticks
    std::string source;      // the file (and family) it came from, for messages
    bool crafting() const { return type == RecipeType::Shaped || type == RecipeType::Shapeless; }
};

class RecipeBook {
public:
    // Every recipe under dataDir/recipes; problems (unknown items and tags, malformed
    // recipes) are listed in `error`, and the rest still load.
    bool load(const std::filesystem::path& dataDir, const ItemRegistry& items, std::string* error);

    const std::vector<Recipe>& recipes() const { return recipes_; }
    // What a `width` x `height` crafting grid (row by row) makes: a stack (with its recipe
    // when one matched; repairs have none), or empty.
    struct Crafted {
        ItemStack result;
        const Recipe* recipe = nullptr;
    };
    Crafted craft(std::span<const ItemStack> grid, int width, int height) const;
    // The recipe a crafting grid matches (null for none, or a repair).
    const Recipe* findCrafting(std::span<const ItemStack> grid, int width, int height) const;
    // The cooking recipe for an input in an appliance, or null.
    const Recipe* findCooking(RecipeType type, const ItemStack& input) const;
    // What an ingredient leaves in the grid (vanilla craftingRemainingItem): buckets of
    // water, lava, milk and powder snow leave a bucket, honey and dragon's breath a bottle.
    ItemStack remainder(const ItemStack& used) const;
    // Crafting recipes that some grid matches along with another giving something else.
    std::vector<std::string> conflicts() const;

private:
    std::vector<Recipe> recipes_;
    std::vector<std::vector<const Recipe*>> cookingByItem_[4]; // per appliance, by item index
    const ItemRegistry* items_ = nullptr;
};

} // namespace mc
