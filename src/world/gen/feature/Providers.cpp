#include "world/gen/feature/Providers.h"

#include <algorithm>
#include <cmath>

#include "world/block/BlockRules.h"
#include "world/gen/feature/Feature.h"

namespace mc {
namespace {

std::string typeOf(const Json& j) {
    std::string t = jsonGet<std::string>(j, "type", "");
    if (t.starts_with("minecraft:")) t.erase(0, 10);
    return t;
}

glm::ivec3 parseOffset(const Json& j) {
    auto it = j.find("offset");
    if (it == j.end() || !it->is_array() || it->size() != 3) return {0, 0, 0};
    return {(*it)[0].get<int>(), (*it)[1].get<int>(), (*it)[2].get<int>()};
}

std::optional<Direction> parseDirection(std::string_view s) {
    static constexpr std::pair<std::string_view, Direction> names[] = {
        {"down", Direction::Down}, {"up", Direction::Up},     {"north", Direction::North},
        {"south", Direction::South}, {"west", Direction::West}, {"east", Direction::East}};
    for (const auto& [n, d] : names)
        if (n == s) return d;
    return std::nullopt;
}

double normal(Xoroshiro& r, double mean, double deviation) { return mean + r.nextGaussian() * deviation; }

} // namespace

double biomeInfoNoise(double x, double z) {
    // Stand-in for vanilla's PerlinSimplexNoise(seed 2345, octave 0); the same
    // field as Biome's swamp grass noise.
    static const PerlinNoise noise(Xoroshiro(2345), 0, {1.0});
    return noise.value(x, z, 0.0);
}

// ---------------------------------------------------------------- IntProvider

IntProvider IntProvider::constant(int v) {
    IntProvider p;
    p.min_ = p.max_ = v;
    return p;
}

IntProvider IntProvider::uniform(int lo, int hi) {
    IntProvider p;
    p.kind_ = Kind::Uniform;
    p.min_ = lo;
    p.max_ = hi;
    return p;
}

std::optional<IntProvider> IntProvider::parse(const Json& j, FeatureLoader& l) {
    if (j.is_number_integer()) return constant(j.get<int>());
    if (j.is_array() && j.size() == 2 && j[0].is_number_integer() && j[1].is_number_integer())
        return uniform(j[0].get<int>(), j[1].get<int>());
    if (!j.is_object()) {
        l.error("expected an int provider");
        return std::nullopt;
    }
    const std::string t = typeOf(j);
    IntProvider p;
    if (t == "constant") return constant(l.getInt(j, "value", 0));
    if (t == "uniform" || t == "biased_to_bottom") {
        p.kind_ = t == "uniform" ? Kind::Uniform : Kind::BiasedToBottom;
        p.min_ = l.getInt(j, "min_inclusive", 0);
        p.max_ = l.getInt(j, "max_inclusive", 0);
        return p;
    }
    if (t == "clamped" || t == "clamped_normal") {
        p.kind_ = t == "clamped" ? Kind::Clamped : Kind::ClampedNormal;
        p.min_ = l.getInt(j, "min_inclusive", 0);
        p.max_ = l.getInt(j, "max_inclusive", 0);
        if (p.kind_ == Kind::Clamped) {
            auto src = j.find("source");
            if (src == j.end()) return std::nullopt;
            auto s = parse(*src, l);
            if (!s) return std::nullopt;
            p.source_ = std::make_shared<IntProvider>(*s);
        } else {
            p.mean_ = l.getFloat(j, "mean", 0);
            p.deviation_ = l.getFloat(j, "deviation", 1);
        }
        return p;
    }
    if (t == "weighted_list") {
        p.kind_ = Kind::WeightedList;
        p.min_ = INT32_MAX;
        p.max_ = INT32_MIN;
        auto dist = j.find("distribution");
        if (dist == j.end() || !dist->is_array()) return std::nullopt;
        for (const auto& e : *dist) {
            auto d = parse(e.value("data", Json()), l);
            const int w = l.getInt(e, "weight", 1);
            if (!d || w <= 0) continue;
            p.min_ = std::min(p.min_, d->min_);
            p.max_ = std::max(p.max_, d->max_);
            p.weighted_.emplace_back(std::make_shared<IntProvider>(*d), w);
            p.totalWeight_ += w;
        }
        if (p.weighted_.empty()) return std::nullopt;
        return p;
    }
    l.error("unknown int provider " + t);
    return std::nullopt;
}

int IntProvider::sample(Xoroshiro& r) const {
    switch (kind_) {
    case Kind::Constant: return min_;
    case Kind::Uniform: return randomBetween(r, min_, max_);
    case Kind::BiasedToBottom: return min_ + r.nextInt(r.nextInt(max_ - min_ + 1) + 1);
    case Kind::Clamped: return std::clamp(source_->sample(r), min_, max_);
    case Kind::ClampedNormal: return int(std::clamp(normal(r, mean_, deviation_), double(min_), double(max_)));
    case Kind::WeightedList: {
        int pick = r.nextInt(totalWeight_);
        for (const auto& [p, w] : weighted_) {
            if (pick < w) return p->sample(r);
            pick -= w;
        }
        return weighted_.back().first->sample(r);
    }
    }
    return min_;
}

// ---------------------------------------------------------------- FloatProvider

FloatProvider FloatProvider::constant(float v) {
    FloatProvider p;
    p.a_ = v;
    return p;
}

std::optional<FloatProvider> FloatProvider::parse(const Json& j, FeatureLoader& l) {
    if (j.is_number()) return constant(j.get<float>());
    if (!j.is_object()) {
        l.error("expected a float provider");
        return std::nullopt;
    }
    const std::string t = typeOf(j);
    FloatProvider p;
    if (t == "constant") return constant(l.getFloat(j, "value", 0));
    if (t == "uniform") {
        p.kind_ = Kind::Uniform;
        p.a_ = l.getFloat(j, "min_inclusive", 0);
        p.b_ = l.getFloat(j, "max_exclusive", 1);
        return p;
    }
    if (t == "clamped_normal") {
        p.kind_ = Kind::ClampedNormal;
        p.a_ = l.getFloat(j, "mean", 0);
        p.b_ = l.getFloat(j, "deviation", 1);
        p.c_ = l.getFloat(j, "min", 0);
        p.d_ = l.getFloat(j, "max", 1);
        return p;
    }
    if (t == "trapezoid") {
        p.kind_ = Kind::Trapezoid;
        p.a_ = l.getFloat(j, "min", 0);
        p.b_ = l.getFloat(j, "max", 1);
        p.c_ = l.getFloat(j, "plateau", 0);
        return p;
    }
    l.error("unknown float provider " + t);
    return std::nullopt;
}

float FloatProvider::sample(Xoroshiro& r) const {
    switch (kind_) {
    case Kind::Constant: return a_;
    case Kind::Uniform: return a_ + r.nextFloat() * (b_ - a_);
    case Kind::ClampedNormal: return std::clamp(float(normal(r, a_, b_)), c_, d_);
    case Kind::Trapezoid: {
        const float range = b_ - a_, half = (range - c_) / 2.0f, rest = range - half;
        const float first = r.nextFloat() * rest;
        return a_ + first + r.nextFloat() * half;
    }
    }
    return a_;
}

// ---------------------------------------------------------------- heights

int VerticalAnchor::resolve() const {
    switch (kind) {
    case Kind::Absolute: return value;
    case Kind::AboveBottom: return kWorldMinY + value;
    case Kind::BelowTop: return kWorldMaxY - value;
    }
    return value;
}

std::optional<VerticalAnchor> VerticalAnchor::parse(const Json& j, FeatureLoader& l) {
    if (j.is_number_integer()) return VerticalAnchor{Kind::Absolute, j.get<int>()};
    if (j.is_object()) {
        if (j.contains("absolute")) return VerticalAnchor{Kind::Absolute, l.getInt(j, "absolute", 0)};
        if (j.contains("above_bottom")) return VerticalAnchor{Kind::AboveBottom, l.getInt(j, "above_bottom", 0)};
        if (j.contains("below_top")) return VerticalAnchor{Kind::BelowTop, l.getInt(j, "below_top", 0)};
    }
    l.error("expected a vertical anchor");
    return std::nullopt;
}

std::optional<HeightProvider> HeightProvider::parse(const Json& j, FeatureLoader& l) {
    HeightProvider p;
    if (!j.is_object() || !j.contains("type")) {
        auto a = VerticalAnchor::parse(j, l);
        if (!a) return std::nullopt;
        p.min_ = p.max_ = *a;
        return p;
    }
    const std::string t = typeOf(j);
    if (t == "constant") {
        auto a = VerticalAnchor::parse(j.value("value", Json()), l);
        if (!a) return std::nullopt;
        p.min_ = p.max_ = *a;
        return p;
    }
    auto lo = VerticalAnchor::parse(j.value("min_inclusive", Json()), l);
    auto hi = VerticalAnchor::parse(j.value("max_inclusive", Json()), l);
    if (!lo || !hi) return std::nullopt;
    p.min_ = *lo;
    p.max_ = *hi;
    if (t == "uniform") p.kind_ = Kind::Uniform;
    else if (t == "trapezoid") p.kind_ = Kind::Trapezoid, p.extra_ = l.getInt(j, "plateau", 0);
    else if (t == "biased_to_bottom") p.kind_ = Kind::BiasedToBottom, p.extra_ = l.getInt(j, "inner", 1);
    else if (t == "very_biased_to_bottom") p.kind_ = Kind::VeryBiasedToBottom, p.extra_ = l.getInt(j, "inner", 1);
    else {
        l.error("unknown height provider " + t);
        return std::nullopt;
    }
    return p;
}

int HeightProvider::sample(Xoroshiro& r) const {
    const int lo = min_.resolve(), hi = max_.resolve();
    switch (kind_) {
    case Kind::Constant: return lo;
    case Kind::Uniform: return lo > hi ? lo : randomBetween(r, lo, hi);
    case Kind::Trapezoid: {
        if (lo > hi) return lo;
        const int range = hi - lo;
        if (extra_ >= range) return randomBetween(r, lo, hi);
        const int half = (range - extra_) / 2, rest = range - half;
        return lo + randomBetween(r, 0, rest) + randomBetween(r, 0, half);
    }
    case Kind::BiasedToBottom: {
        if (hi - lo - extra_ + 1 <= 0) return lo;
        const int k = r.nextInt(hi - lo - extra_ + 1);
        return r.nextInt(k + extra_) + lo;
    }
    case Kind::VeryBiasedToBottom: {
        if (hi - lo - extra_ + 1 <= 0) return lo;
        const int k = randomBetween(r, lo + extra_, hi);
        const int m = randomBetween(r, lo, k - 1);
        return randomBetween(r, lo, m - 1 + extra_);
    }
    }
    return lo;
}

// ---------------------------------------------------------------- block states

BlockStateProvider BlockStateProvider::simple(StateId s) {
    BlockStateProvider p;
    p.state_ = s;
    return p;
}

std::optional<BlockStateProvider> BlockStateProvider::parse(const Json& j, FeatureLoader& l) {
    BlockStateProvider p;
    p.registry_ = &l.registry;
    // Shorthands: a state string, or a vanilla {"Name": ...} state.
    if (j.is_string() || (j.is_object() && j.contains("Name"))) {
        auto s = l.state(j);
        if (!s) return std::nullopt;
        return simple(*s);
    }
    if (!j.is_object()) {
        l.error("expected a block state provider");
        return std::nullopt;
    }
    const std::string t = typeOf(j);
    auto states = [&](const char* key, std::vector<StateId>& out) {
        auto it = j.find(key);
        if (it == j.end() || !it->is_array()) return;
        for (const auto& e : *it)
            if (auto s = l.state(e)) out.push_back(*s);
    };
    auto makeNoise = [&](const char* key, uint64_t seed) {
        const Json n = j.value(key, Json::object());
        const int first = l.getInt(n, "firstOctave", 0);
        std::vector<double> amps;
        if (auto a = n.find("amplitudes"); a != n.end() && a->is_array())
            for (const auto& v : *a) amps.push_back(v.get<double>());
        if (amps.empty()) amps = {1.0};
        return std::make_shared<NormalNoise>(Xoroshiro(int64_t(seed)), "provider", first, amps);
    };
    if (t == "simple_state_provider") {
        auto s = l.state(j.value("state", Json()));
        if (!s) return std::nullopt;
        return simple(*s);
    }
    if (t == "rotated_block_provider") {
        auto s = l.state(j.value("state", Json()));
        if (!s) return std::nullopt;
        p.kind_ = Kind::Rotated;
        p.state_ = *s;
        return p;
    }
    if (t == "weighted_state_provider") {
        p.kind_ = Kind::Weighted;
        for (const auto& e : j.value("entries", Json::array())) {
            auto s = l.state(e.value("data", Json()));
            const int w = l.getInt(e, "weight", 1);
            if (!s || w <= 0) continue;
            p.weighted_.emplace_back(*s, w);
            p.totalWeight_ += w;
        }
        if (p.weighted_.empty()) return std::nullopt;
        return p;
    }
    if (t == "noise_provider" || t == "noise_threshold_provider" || t == "dual_noise_provider") {
        p.noise_ = std::make_shared<NoiseSettings>();
        const uint64_t seed = uint64_t(j.value("seed", int64_t(0)));
        p.noise_->noise = makeNoise("noise", seed);
        p.noise_->scale = l.getFloat(j, "scale", 1.0f);
        if (t == "noise_threshold_provider") {
            p.kind_ = Kind::NoiseThreshold;
            p.noise_->threshold = l.getFloat(j, "threshold", 0.0f);
            p.noise_->highChance = l.getFloat(j, "high_chance", 0.0f);
            auto d = l.state(j.value("default_state", Json()));
            if (!d) return std::nullopt;
            p.state_ = *d;
            states("low_states", p.lowStates_);
            states("high_states", p.highStates_);
            if (p.lowStates_.empty() || p.highStates_.empty()) return std::nullopt;
            return p;
        }
        states("states", p.states_);
        if (p.states_.empty()) return std::nullopt;
        if (t == "dual_noise_provider") {
            p.kind_ = Kind::DualNoise;
            p.noise_->slow = makeNoise("slow_noise", seed);
            p.noise_->slowScale = l.getFloat(j, "slow_scale", 1.0f);
            auto v = IntProvider::parse(j.value("variety", Json::array({1, 1})), l);
            if (!v) return std::nullopt;
            p.noise_->variety = *v;
        } else {
            p.kind_ = Kind::Noise;
        }
        return p;
    }
    if (t == "randomized_int_state_provider") {
        auto src = parse(j.value("source", Json()), l);
        auto values = IntProvider::parse(j.value("values", Json()), l);
        if (!src || !values) return std::nullopt;
        p.kind_ = Kind::RandomizedInt;
        p.source_ = std::make_shared<BlockStateProvider>(*src);
        p.property_ = jsonGet<std::string>(j, "property", "");
        p.values_ = *values;
        return p;
    }
    l.error("unknown block state provider " + t);
    return std::nullopt;
}

double BlockStateProvider::noiseAt(const NormalNoise& n, const BlockPos& p, double scale) const {
    return n.value(p.x * scale, p.y * scale, p.z * scale);
}

StateId BlockStateProvider::pickByNoise(const std::vector<StateId>& states, double noise) const {
    const double e = std::clamp((1.0 + noise) / 2.0, 0.0, 0.9999);
    return states[size_t(e * double(states.size()))];
}

StateId BlockStateProvider::get(Xoroshiro& r, const BlockPos& pos) const {
    switch (kind_) {
    case Kind::Simple: return state_;
    case Kind::Weighted: {
        int pick = r.nextInt(totalWeight_);
        for (const auto& [s, w] : weighted_) {
            if (pick < w) return s;
            pick -= w;
        }
        return weighted_.back().first;
    }
    case Kind::Rotated: {
        static constexpr const char* axes[] = {"x", "y", "z"};
        return registry_->with(state_, "axis", axes[r.nextInt(3)]).value_or(state_);
    }
    case Kind::Noise: return pickByNoise(states_, noiseAt(*noise_->noise, pos, noise_->scale));
    case Kind::NoiseThreshold: {
        const double d = noiseAt(*noise_->noise, pos, noise_->scale);
        if (d < noise_->threshold) return lowStates_[size_t(r.nextInt(int(lowStates_.size())))];
        if (r.nextFloat() < noise_->highChance) return highStates_[size_t(r.nextInt(int(highStates_.size())))];
        return state_;
    }
    case Kind::DualNoise: {
        const double slow = noiseAt(*noise_->slow, pos, noise_->slowScale);
        const double t = std::clamp((slow + 1.0) / 2.0, 0.0, 1.0);
        const int lo = noise_->variety.minValue(), hi = noise_->variety.maxValue() + 1;
        const int count = std::max(1, int(lo + t * (hi - lo)));
        std::vector<StateId> list;
        list.reserve(size_t(count));
        for (int j = 0; j < count; ++j)
            list.push_back(pickByNoise(states_, noiseAt(*noise_->slow, pos.offset(j * 54545, 0, j * 34234), noise_->slowScale)));
        return pickByNoise(list, noiseAt(*noise_->noise, pos, noise_->scale));
    }
    case Kind::RandomizedInt: {
        const StateId base = source_->get(r, pos);
        return registry_->with(base, property_, std::to_string(values_.sample(r))).value_or(base);
    }
    }
    return state_;
}

// ---------------------------------------------------------------- predicates

BlockPredicate BlockPredicate::alwaysTrue() { return BlockPredicate(); }

BlockPredicate BlockPredicate::matchingBlock(const Block& b, glm::ivec3 offset) {
    BlockPredicate p;
    p.kind_ = Kind::MatchingBlocks;
    p.blocks_ = {b.index};
    p.offset_ = offset;
    return p;
}

BlockPredicate BlockPredicate::air(glm::ivec3 offset) {
    BlockPredicate p;
    p.kind_ = Kind::Air;
    p.offset_ = offset;
    return p;
}

std::optional<BlockPredicate> BlockPredicate::parse(const Json& j, FeatureLoader& l) {
    if (!j.is_object()) {
        l.error("expected a block predicate");
        return std::nullopt;
    }
    const std::string t = typeOf(j);
    BlockPredicate p;
    p.offset_ = parseOffset(j);
    if (t == "true" || t == "unobstructed") return p;
    if (t == "matching_blocks") {
        p.kind_ = Kind::MatchingBlocks;
        const Json b = j.value("blocks", Json());
        if (b.is_string() && b.get<std::string>().starts_with('#')) {
            auto tag = l.tag(b);
            if (!tag) return std::nullopt;
            p.kind_ = Kind::MatchingTag;
            p.tag_ = *tag;
            return p;
        }
        for (const auto& e : b.is_array() ? b : Json::array({b}))
            if (const Block* blk = l.block(e)) p.blocks_.push_back(blk->index);
        return p;
    }
    if (t == "matching_block_tag") {
        auto tag = l.tag(j.value("tag", Json()));
        if (!tag) return std::nullopt;
        p.kind_ = Kind::MatchingTag;
        p.tag_ = *tag;
        return p;
    }
    if (t == "matching_fluids") {
        p.kind_ = Kind::MatchingFluids;
        const Json f = j.value("fluids", Json());
        for (const auto& e : f.is_array() ? f : Json::array({f})) {
            std::string name = e.get<std::string>();
            if (name.starts_with("minecraft:")) name.erase(0, 10);
            if (name == "water" || name == "flowing_water") p.fluids_.push_back(FluidKind::Water);
            else if (name == "lava" || name == "flowing_lava") p.fluids_.push_back(FluidKind::Lava);
            else if (name == "empty") p.fluids_.push_back(FluidKind::None);
        }
        return p;
    }
    if (t == "solid") return p.kind_ = Kind::Solid, p;
    if (t == "replaceable") return p.kind_ = Kind::Replaceable, p;
    if (t == "inside_world_bounds") return p.kind_ = Kind::InWorld, p;
    if (t == "would_survive") {
        auto s = l.state(j.value("state", Json()));
        if (!s) return std::nullopt;
        p.kind_ = Kind::WouldSurvive;
        p.state_ = *s;
        return p;
    }
    if (t == "has_sturdy_face") {
        auto d = parseDirection(jsonGet<std::string>(j, "direction", "up"));
        if (!d) return std::nullopt;
        p.kind_ = Kind::Sturdy;
        p.face_ = *d;
        return p;
    }
    if (t == "not") {
        auto c = parse(j.value("predicate", Json()), l);
        if (!c) return std::nullopt;
        p.kind_ = Kind::Not;
        p.children_.push_back(std::move(*c));
        return p;
    }
    if (t == "all_of" || t == "any_of") {
        p.kind_ = t == "all_of" ? Kind::AllOf : Kind::AnyOf;
        for (const auto& e : j.value("predicates", Json::array())) {
            auto c = parse(e, l);
            if (!c) return std::nullopt;
            p.children_.push_back(std::move(*c));
        }
        return p;
    }
    l.error("unknown block predicate " + t);
    return std::nullopt;
}

bool BlockPredicate::test(const WorldGenRegion& level, const BlockPos& pos) const {
    const BlockPos p = pos.offset(offset_.x, offset_.y, offset_.z);
    switch (kind_) {
    case Kind::True: return true;
    case Kind::Air: return level.isAir(p);
    case Kind::MatchingBlocks: {
        const uint16_t b = level.info(p).block;
        return std::find(blocks_.begin(), blocks_.end(), b) != blocks_.end();
    }
    case Kind::MatchingTag: return level.hasTag(p, tag_);
    case Kind::MatchingFluids: {
        const FluidKind f = level.fluid(p).kind;
        return std::find(fluids_.begin(), fluids_.end(), f) != fluids_.end();
    }
    case Kind::Solid: return (level.info(p).flags & kBlocksMotion) != 0;
    case Kind::Replaceable: return (level.info(p).flags & (kReplaceable | kAir)) != 0;
    case Kind::WouldSurvive: return level.registry().rules().canSurvive(level, state_, p);
    case Kind::Sturdy: return level.sturdy(p, face_);
    case Kind::InWorld: return WorldGenRegion::inBuildHeight(p.y);
    case Kind::Not: return !children_[0].test(level, pos);
    case Kind::AllOf:
        for (const auto& c : children_)
            if (!c.test(level, pos)) return false;
        return true;
    case Kind::AnyOf:
        for (const auto& c : children_)
            if (c.test(level, pos)) return true;
        return false;
    }
    return false;
}

bool RuleTest::test(const BlockRegistry& reg, StateId s) const {
    switch (kind) {
    case Kind::Always: return true;
    case Kind::Tag: return reg.hasTag(s, tag);
    case Kind::Block: return reg.info(s).block == block;
    case Kind::State: return s == state;
    }
    return false;
}

std::optional<RuleTest> RuleTest::parse(const Json& j, FeatureLoader& l) {
    RuleTest t;
    if (j.is_string() && (j == "always" || j == "always_true")) return t;
    if (j.is_object()) {
        if (j.contains("tag")) {
            auto tag = l.tag(j["tag"]);
            if (!tag) return std::nullopt;
            t.kind = Kind::Tag;
            t.tag = *tag;
            return t;
        }
        if (j.contains("block")) {
            const Block* b = l.block(j["block"]);
            if (!b) return std::nullopt;
            t.kind = Kind::Block;
            t.block = b->index;
            return t;
        }
        if (j.contains("state")) {
            auto s = l.state(j["state"]);
            if (!s) return std::nullopt;
            t.kind = Kind::State;
            t.state = *s;
            return t;
        }
    }
    l.error("expected a rule test ({tag}, {block}, {state} or \"always\")");
    return std::nullopt;
}

} // namespace mc
