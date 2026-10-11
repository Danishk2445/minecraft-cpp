#include "resources/BlockModels.h"

#include <format>

#include "core/Rng.h"

namespace mc {

const BakedModel* BakedChoice::pick(int roll) const {
    for (size_t i = 0; i < models.size(); ++i) {
        roll -= weights[i];
        if (roll < 0) return models[i];
    }
    return models.back();
}

bool BlockModels::load(const ResourcePack& pack, ModelStore& models, ModelBaker& baker) {
    defs_.clear();
    index_.clear();
    baked_.clear();
    for (const auto& name : pack.list("minecraft", "blockstates", ".json")) {
        std::string err;
        const auto j = pack.json(pack.file("minecraft", "blockstates/" + name + ".json"), &err);
        if (!j) {
            errors_.push_back(err);
            continue;
        }
        auto def = BlockStateDef::parse("minecraft:" + name, *j, &err);
        if (!def) {
            errors_.push_back(err);
            continue;
        }
        index_.emplace(def->block, defs_.size());
        defs_.push_back(std::move(*def));
    }

    auto bakeChoice = [&](const VariantChoice& c) {
        BakedChoice b;
        for (const ModelVariant& v : c.options) {
            b.models.push_back(baker.bake(v));
            b.weights.push_back(v.weight);
            b.totalWeight += v.weight;
        }
        baked_.emplace(&c, std::move(b));
    };
    for (const auto& def : defs_) {
        for (const auto& v : def.variants) bakeChoice(v.choice);
        for (const auto& p : def.parts) bakeChoice(p.apply);
    }
    (void)models;
    return errors_.empty();
}

const BlockStateDef* BlockModels::find(std::string_view blockId) const {
    auto it = index_.find(std::string(blockId));
    return it == index_.end() ? nullptr : &defs_[it->second];
}

const BakedChoice& BlockModels::baked(const VariantChoice& choice) const { return baked_.at(&choice); }

void BlockModels::collect(std::span<const BakedChoice* const> choices, const BlockPos& pos,
                          std::vector<const BakedModel*>& out) {
    if (choices.empty()) return;
    JavaRandom random(positionSeed(pos.x, pos.y, pos.z));
    if (choices.size() == 1) {
        const BakedChoice& c = *choices[0];
        out.push_back(c.models.size() == 1 ? c.models[0] : c.pick(random.nextInt(c.totalWeight)));
        return;
    }
    // Multipart: every part restarts from the same derived seed.
    const int64_t seed = random.nextLong();
    for (const BakedChoice* c : choices) {
        if (c->models.size() == 1) {
            out.push_back(c->models[0]);
            continue;
        }
        random.setSeed(seed);
        out.push_back(c->pick(random.nextInt(c->totalWeight)));
    }
}

} // namespace mc
