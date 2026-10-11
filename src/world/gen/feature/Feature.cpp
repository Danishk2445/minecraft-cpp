#include "world/gen/feature/Feature.h"

#include <algorithm>
#include <format>

namespace mc {
namespace {

std::string stripNamespace(std::string s) {
    if (s.starts_with("minecraft:")) s.erase(0, 10);
    return s;
}

} // namespace

// ---------------------------------------------------------------- PlacedFeature

bool PlacedFeature::place(PlacementContext& ctx, Xoroshiro& random, const BlockPos& origin) const {
    return placeFrom(0, ctx, random, origin);
}

// Depth first, like vanilla's lazily flat-mapped streams: each position runs the
// rest of the chain (and the feature) before the next one is produced, so later
// positions see earlier placements.
bool PlacedFeature::placeFrom(size_t m, PlacementContext& ctx, Xoroshiro& random, const BlockPos& pos) const {
    if (m == placement.size()) {
        FeatureContext fc{ctx.level, random, pos, ctx};
        return feature->place(fc);
    }
    std::vector<BlockPos> positions;
    placement[m]->apply(ctx, random, pos, positions);
    bool any = false;
    for (const BlockPos& p : positions) any |= placeFrom(m + 1, ctx, random, p);
    return any;
}

// ---------------------------------------------------------------- FeatureLoader

void FeatureLoader::error(const std::string& message) { set_.errors_.push_back(std::format("{}: {}", where, message)); }

std::optional<StateId> FeatureLoader::state(const Json& j) {
    if (j.is_string()) {
        std::string err;
        auto s = registry.parse(j.get<std::string>(), &err);
        if (!s) error(std::format("bad block state '{}': {}", j.get<std::string>(), err));
        return s;
    }
    if (j.is_object() && j.contains("Name")) {
        std::string text = stripNamespace(j["Name"].get<std::string>());
        if (auto props = j.find("Properties"); props != j.end() && props->is_object() && !props->empty()) {
            text += '[';
            bool first = true;
            for (auto it = props->begin(); it != props->end(); ++it) {
                if (!first) text += ',';
                first = false;
                text += it.key() + "=" + (it->is_string() ? it->get<std::string>() : it->dump());
            }
            text += ']';
        }
        std::string err;
        auto s = registry.parse(text, &err);
        if (!s) error(std::format("bad block state '{}': {}", text, err));
        return s;
    }
    error("expected a block state");
    return std::nullopt;
}

std::optional<TagId> FeatureLoader::tag(const Json& j) {
    if (!j.is_string()) {
        error("expected a tag name");
        return std::nullopt;
    }
    auto t = registry.tag(j.get<std::string>());
    if (!t) error("unknown tag " + j.get<std::string>());
    return t;
}

const Block* FeatureLoader::block(const Json& j) {
    if (!j.is_string()) {
        error("expected a block id");
        return nullptr;
    }
    const Block* b = registry.find(stripNamespace(j.get<std::string>()));
    if (!b) error("unknown block " + j.get<std::string>());
    return b;
}

const PlacedFeature* FeatureLoader::placed(const Json& j) {
    if (j.is_string()) {
        const std::string name = stripNamespace(j.get<std::string>());
        auto it = set_.definitions_.find(name);
        if (it == set_.definitions_.end()) {
            error("unknown feature " + name);
            return nullptr;
        }
        const std::string saved = where;
        const PlacedFeature* p = set_.build(name, it->second, *this);
        where = saved;
        return p;
    }
    if (j.is_object()) {
        const std::string saved = where;
        const PlacedFeature* p = set_.build(saved + "/inline", j, *this);
        where = saved;
        return p;
    }
    error("expected a feature name or definition");
    return nullptr;
}

int FeatureLoader::getInt(const Json& j, const char* key, int fallback) {
    auto it = j.find(key);
    if (it == j.end()) return fallback;
    if (!it->is_number()) {
        error(std::format("'{}' should be a number", key));
        return fallback;
    }
    return it->get<int>();
}

float FeatureLoader::getFloat(const Json& j, const char* key, float fallback) {
    auto it = j.find(key);
    if (it == j.end()) return fallback;
    if (!it->is_number()) {
        error(std::format("'{}' should be a number", key));
        return fallback;
    }
    return it->get<float>();
}

bool FeatureLoader::getBool(const Json& j, const char* key, bool fallback) {
    auto it = j.find(key);
    if (it == j.end()) return fallback;
    if (!it->is_boolean()) {
        error(std::format("'{}' should be true or false", key));
        return fallback;
    }
    return it->get<bool>();
}

// ---------------------------------------------------------------- FeatureSet

FeatureSet::FeatureSet() {
    registerPlacementModifiers(*this);
    registerBasicFeatures(*this);
    registerTreeFeatures(*this);
    registerVegetationFeatures(*this);
    registerCaveFeatures(*this);
}

FeatureSet::~FeatureSet() = default;

const PlacedFeature* FeatureSet::find(std::string_view name) const {
    auto it = byName_.find(stripNamespace(std::string(name)));
    return it == byName_.end() ? nullptr : it->second;
}

const PlacedFeature* FeatureSet::build(const std::string& name, const Json& def, FeatureLoader& loader) {
    const bool named = definitions_.contains(name);
    if (named) {
        if (auto it = byName_.find(name); it != byName_.end()) return it->second;
        if (std::find(failed_.begin(), failed_.end(), name) != failed_.end()) return nullptr;
        if (std::find(building_.begin(), building_.end(), name) != building_.end()) {
            loader.where = name;
            loader.error("feature refers to itself");
            return nullptr;
        }
        building_.push_back(name);
    }
    loader.where = name;
    const PlacedFeature* result = nullptr;
    std::string type = stripNamespace(jsonGet<std::string>(def, "type", ""));
    auto factory = featureTypes_.find(type);
    if (factory == featureTypes_.end()) {
        loader.error(std::format("unknown feature type '{}'", type));
    } else if (auto feature = factory->second(def.value("config", Json::object()), loader)) {
        loader.where = name;
        auto placed = std::make_unique<PlacedFeature>();
        placed->name = name;
        placed->feature = feature.get();
        bool ok = true;
        for (const Json& m : def.value("placement", Json::array())) {
            const std::string mt = stripNamespace(jsonGet<std::string>(m, "type", ""));
            auto mf = modifierTypes_.find(mt);
            if (mf == modifierTypes_.end()) {
                loader.error(std::format("unknown placement modifier '{}'", mt));
                ok = false;
                continue;
            }
            if (auto modifier = mf->second(m, loader)) placed->placement.push_back(std::move(modifier));
            else ok = false;
        }
        if (ok) {
            features_.push_back(std::move(feature));
            result = placed.get();
            placed_.push_back(std::move(placed));
        }
    } else {
        loader.where = name;
        loader.error("invalid configuration");
    }
    if (named) {
        building_.pop_back();
        if (result) byName_.emplace(name, result);
        else failed_.push_back(name);
    }
    return result;
}

bool FeatureSet::load(const std::filesystem::path& dir, const BlockRegistry& registry, std::string* errors) {
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::end(it); it.increment(ec))
        if (it->path().extension() == ".json") files.push_back(it->path());
    std::sort(files.begin(), files.end());
    if (files.empty()) errors_.push_back("no feature files in " + dir.string());
    for (const auto& f : files) {
        std::string err;
        auto j = loadJsonFile(f, &err);
        if (!j || !j->is_object()) {
            errors_.push_back(err.empty() ? f.string() + ": expected an object" : err);
            continue;
        }
        for (auto it = j->begin(); it != j->end(); ++it) {
            if (it.key().starts_with('_')) continue;
            if (!definitions_.emplace(stripNamespace(it.key()), *it).second)
                errors_.push_back(std::format("{}: feature '{}' is defined twice", f.string(), it.key()));
        }
    }
    FeatureLoader loader(registry, *this);
    std::vector<std::string> names;
    for (const auto& [name, def] : definitions_) names.push_back(name);
    std::sort(names.begin(), names.end());
    for (const auto& name : names) build(name, definitions_[name], loader);
    if (errors) {
        errors->clear();
        for (const auto& e : errors_) *errors += e + "\n";
    }
    return errors_.empty();
}

} // namespace mc
