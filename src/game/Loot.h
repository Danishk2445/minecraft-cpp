// Loot.h -- what blocks drop (data/loot/blocks.json): per block the first rule whose
// match and state conditions fit, with drops (counts, ranges, binomials, chances, counts
// from a property), separate drops for shears, and experience. Blocks without a rule
// drop their own item.
#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/Rng.h"
#include "game/Inventory.h"

namespace mc {

class BlockLoot {
public:
    bool load(const std::filesystem::path& file, const BlockRegistry& blocks, const ItemRegistry& items,
              std::string* error);
    // Breaking `state` (harvested with the right tool, if it needs one); `shears` when
    // broken with shears. Stacks are split to their maximum size.
    std::vector<ItemStack> drops(StateId state, bool shears, Xoroshiro& random) const;
    // Experience points it drops.
    int experience(StateId state, Xoroshiro& random) const;
    // Whether a block removed by a neighbor update (a torch losing its wall) drops too.
    bool dropsWhenUpdated(StateId state) const;

private:
    struct Count {
        int min = 1, max = 1;            // uniform
        int binomialN = 0;               // + binomial(n, p)
        float binomialP = 0.0f;
        std::string fromProperty;        // the count is this property's value
    };
    struct Drop {
        const Item* item = nullptr; // null: the block's own item ("self")
        Count count;
        float chance = 1.0f;
        std::shared_ptr<Drop> otherwise; // rolled when the chance fails ("else")
    };
    struct Rule {
        std::vector<Drop> drops, shears;
        bool hasShears = false;
        bool playerOnly = false; // drops only when a player breaks it, not when an update removes it
        int xpMin = 0, xpMax = 0;
    };
    void rollOne(const Drop& d, StateId state, Xoroshiro& random, std::vector<ItemStack>& out) const;
    std::vector<ItemStack> roll(const std::vector<Drop>& drops, StateId state, Xoroshiro& random) const;

    const BlockRegistry* blocks_ = nullptr;
    const ItemRegistry* items_ = nullptr;
    std::vector<Rule> rules_;
    std::vector<int16_t> ruleOfState_; // per state, a rule index or -1 (its own item)
};

} // namespace mc
