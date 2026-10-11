#include "world/gen/feature/BiomeDecorator.h"

#include <algorithm>
#include <format>
#include <functional>
#include <map>
#include <set>

#include "resources/ResourcePack.h"
#include "world/gen/feature/DecorationRegion.h"

namespace mc {

bool BiomeDecorator::load(const std::filesystem::path& dataDir, const BlockRegistry& blocks, const BiomeRegistry& biomes,
                          std::string* errors) {
    std::vector<std::string> problems;
    std::string err;
    if (!features_.load(dataDir / "worldgen" / "features", blocks, &err)) {
        for (size_t start = 0; start < err.size();) {
            const size_t end = err.find('\n', start);
            if (end > start) problems.push_back(err.substr(start, end - start));
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }
    auto j = loadJsonFile(dataDir / "worldgen" / "biome_features.json", &err);
    if (!j || !j->is_object()) {
        if (errors) *errors = err.empty() ? "biome_features.json: expected an object" : err;
        return false;
    }
    for (const auto& s : j->value("steps", Json::array())) steps_.push_back(s.get<std::string>());
    const Json sets = j->value("sets", Json::object());

    // Resolve each set into per-step feature lists (included sets first).
    using Lists = std::vector<std::vector<const PlacedFeature*>>;
    std::map<std::string, Lists> resolved;
    std::set<std::string> resolving;
    std::function<const Lists*(const std::string&)> resolve = [&](const std::string& name) -> const Lists* {
        if (auto it = resolved.find(name); it != resolved.end()) return &it->second;
        auto def = sets.find(name);
        if (def == sets.end()) {
            problems.push_back("unknown feature set " + name);
            return nullptr;
        }
        if (!resolving.insert(name).second) {
            problems.push_back("feature set " + name + " includes itself");
            return nullptr;
        }
        Lists lists(steps_.size());
        // A feature appears once per step (repeats would read as an ordering cycle).
        auto append = [&](size_t step, const PlacedFeature* f) {
            if (std::find(lists[step].begin(), lists[step].end(), f) == lists[step].end()) lists[step].push_back(f);
        };
        for (const auto& inc : def->value("include", Json::array()))
            if (const Lists* other = resolve(inc.get<std::string>()))
                for (size_t s = 0; s < steps_.size(); ++s)
                    for (const PlacedFeature* f : (*other)[s]) append(s, f);
        for (auto it = def->begin(); it != def->end(); ++it) {
            if (it.key() == "include" || it.key().starts_with('_')) continue;
            auto step = std::find(steps_.begin(), steps_.end(), it.key());
            if (step == steps_.end()) {
                problems.push_back(std::format("feature set {}: unknown step {}", name, it.key()));
                continue;
            }
            for (const auto& f : *it) {
                const PlacedFeature* pf = features_.find(f.get<std::string>());
                if (!pf) problems.push_back(std::format("feature set {}: unknown feature {}", name, f.get<std::string>()));
                else append(size_t(step - steps_.begin()), pf);
            }
        }
        resolving.erase(name);
        return &resolved.emplace(name, std::move(lists)).first->second;
    };

    // Each biome's set.
    std::vector<const Lists*> biomeLists(biomes.size(), nullptr);
    for (size_t b = 0; b < biomes.size(); ++b) {
        const Biome& bio = biomes.get(BiomeId(b));
        biomeLists[b] = resolve(bio.features.empty() ? std::string(shortId(bio.id)) : bio.features);
    }

    // Global order per step: Kahn's algorithm over "a comes before b in some
    // list", ties broken by first appearance (sets in name order).
    order_.assign(steps_.size(), {});
    for (size_t s = 0; s < steps_.size(); ++s) {
        std::vector<const PlacedFeature*> nodes;
        std::map<const PlacedFeature*, size_t> id;
        std::vector<std::set<size_t>> next;
        std::vector<int> incoming;
        auto node = [&](const PlacedFeature* f) {
            auto [it, inserted] = id.emplace(f, nodes.size());
            if (inserted) {
                nodes.push_back(f);
                next.emplace_back();
                incoming.push_back(0);
            }
            return it->second;
        };
        for (const auto& [name, lists] : resolved) {
            const auto& l = lists[s];
            for (size_t i = 0; i < l.size(); ++i) {
                const size_t a = node(l[i]);
                if (i > 0) {
                    const size_t prev = id[l[i - 1]];
                    if (prev != a && next[prev].insert(a).second) ++incoming[a];
                }
            }
        }
        std::set<size_t> ready;
        for (size_t i = 0; i < nodes.size(); ++i)
            if (incoming[i] == 0) ready.insert(i);
        while (!ready.empty()) {
            const size_t n = *ready.begin();
            ready.erase(ready.begin());
            order_[s].push_back(nodes[n]);
            for (size_t m : next[n])
                if (--incoming[m] == 0) ready.insert(m);
        }
        if (order_[s].size() != nodes.size())
            problems.push_back(std::format("step {}: feature sets order features inconsistently", steps_[s]));
    }

    // Membership bits.
    stepBase_.assign(steps_.size(), 0);
    size_t bits = 0;
    for (size_t s = 0; s < steps_.size(); ++s) {
        stepBase_[s] = bits;
        bits += order_[s].size();
    }
    membership_.assign(biomes.size(), std::vector<uint64_t>((bits + 63) / 64, 0));
    for (size_t b = 0; b < biomes.size(); ++b) {
        if (!biomeLists[b]) continue;
        for (size_t s = 0; s < steps_.size(); ++s)
            for (const PlacedFeature* f : (*biomeLists[b])[s]) {
                const auto it = std::find(order_[s].begin(), order_[s].end(), f);
                const size_t bit = stepBase_[s] + size_t(it - order_[s].begin());
                membership_[b][bit >> 6] |= 1ull << (bit & 63);
            }
    }

    if (errors) {
        errors->clear();
        for (const auto& p : problems) *errors += p + "\n";
    }
    return problems.empty();
}

std::optional<size_t> BiomeDecorator::stepIndex(std::string_view name) const {
    auto it = std::find(steps_.begin(), steps_.end(), name);
    if (it == steps_.end()) return std::nullopt;
    return size_t(it - steps_.begin());
}

bool BiomeDecorator::biomeHas(BiomeId biome, uint8_t step, uint16_t feature) const {
    if (biome >= membership_.size() || step >= stepBase_.size()) return false;
    const size_t bit = stepBase_[step] + feature;
    return (membership_[biome][bit >> 6] >> (bit & 63)) & 1u;
}

void BiomeDecorator::decorate(DecorationRegion& region) const {
    const ChunkPos c = region.center();
    // Biomes anywhere in the chunk (vanilla collects them from every section).
    std::vector<bool> present(membership_.size(), false);
    // The region reads TERRAIN snapshots; biome storage is per 4x4x4 cell.
    for (int y = kWorldMinY; y <= kWorldMaxY; y += 4)
        for (int z = 0; z < 16; z += 4)
            for (int x = 0; x < 16; x += 4) {
                const BiomeId b = region.biome({c.minBlockX() + x, y, c.minBlockZ() + z});
                if (b < present.size()) present[b] = true;
            }
    std::vector<BiomeId> biomes;
    for (size_t b = 0; b < present.size(); ++b)
        if (present[b]) biomes.push_back(BiomeId(b));

    const BlockPos origin(c.minBlockX(), kWorldMinY, c.minBlockZ());
    const uint64_t chunkSeed =
        mixStafford13(uint64_t(region.seed()) ^ mixStafford13(uint64_t(c.key()) + 0x5DEECE66Dull));
    for (size_t s = 0; s < steps_.size(); ++s)
        for (size_t i = 0; i < order_[s].size(); ++i) {
            bool any = false;
            for (BiomeId b : biomes) any |= biomeHas(b, uint8_t(s), uint16_t(i));
            if (!any) continue;
            // Vanilla seeds each (step, feature) from the chunk's decoration seed.
            Xoroshiro random(mixStafford13(chunkSeed + i + 10000 * s), chunkSeed ^ 0x9E3779B97F4A7C15ull);
            region.setFeature(uint8_t(s), uint16_t(i));
            PlacementContext ctx{region, this, uint8_t(s), uint16_t(i)};
            order_[s][i]->place(ctx, random, origin);
        }
}

} // namespace mc
