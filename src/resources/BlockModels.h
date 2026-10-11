// BlockModels.h -- every blockstate file in the pack, parsed and baked.
#pragma once

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/Math.h"
#include "resources/Atlas.h"
#include "resources/BlockStateDef.h"
#include "resources/ModelBaker.h"

namespace mc {

// A baked VariantChoice: weighted random models (usually just one).
struct BakedChoice {
    std::vector<const BakedModel*> models;
    std::vector<int> weights;
    int totalWeight = 0;

    const BakedModel* pick(int roll) const; // roll in [0, totalWeight)
};

class BlockModels {
public:
    // Parses every blockstates/*.json and bakes every variant it references.
    bool load(const ResourcePack& pack, ModelStore& models, ModelBaker& baker);

    const std::vector<BlockStateDef>& definitions() const { return defs_; }
    const BlockStateDef* find(std::string_view blockId) const;
    const BakedChoice& baked(const VariantChoice& choice) const;

    // Choices that apply to a state; `get(property)` returns its value.
    template <class Lookup>
    std::vector<const BakedChoice*> choicesFor(const BlockStateDef& def, const Lookup& get) const {
        std::vector<const VariantChoice*> raw;
        def.select(get, raw);
        std::vector<const BakedChoice*> out;
        out.reserve(raw.size());
        for (const VariantChoice* c : raw) out.push_back(&baked(*c));
        return out;
    }

    // Vanilla per-position variant selection (JavaRandom seeded with Mth.getSeed):
    // appends one model per choice.
    static void collect(std::span<const BakedChoice* const> choices, const BlockPos& pos,
                        std::vector<const BakedModel*>& out);

    const std::vector<std::string>& errors() const { return errors_; }

private:
    std::vector<BlockStateDef> defs_;
    std::unordered_map<std::string, size_t> index_;
    std::unordered_map<const VariantChoice*, BakedChoice> baked_;
    std::vector<std::string> errors_;
};

} // namespace mc
