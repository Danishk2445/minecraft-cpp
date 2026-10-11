// Providers.h -- vanilla worldgen value sources: int/float/height providers,
// block-state providers and block predicates, parsed from feature JSON.
#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "core/Json.h"
#include "core/Noise.h"
#include "core/Rng.h"
#include "world/gen/feature/WorldGenRegion.h"

namespace mc {

class FeatureLoader;

// Vanilla Mth.nextInt(random, min, max): min if max <= min.
inline int randomBetween(Xoroshiro& r, int lo, int hi) { return lo >= hi ? lo : r.nextInt(hi - lo + 1) + lo; }

// IntProvider: constant, uniform, biased_to_bottom, clamped, clamped_normal, weighted_list.
// JSON: a number, [min, max], or vanilla {"type": "uniform", "min_inclusive", "max_inclusive"}.
class IntProvider {
public:
    IntProvider() = default;
    static IntProvider constant(int v);
    static IntProvider uniform(int lo, int hi);
    static std::optional<IntProvider> parse(const Json& j, FeatureLoader& loader);
    int sample(Xoroshiro& r) const;
    int minValue() const { return min_; }
    int maxValue() const { return max_; }

private:
    enum class Kind : uint8_t { Constant, Uniform, BiasedToBottom, Clamped, ClampedNormal, WeightedList };
    Kind kind_ = Kind::Constant;
    int min_ = 0, max_ = 0;
    float mean_ = 0, deviation_ = 0;
    std::shared_ptr<const IntProvider> source_;
    std::vector<std::pair<std::shared_ptr<const IntProvider>, int>> weighted_;
    int totalWeight_ = 0;
};

// FloatProvider: constant, uniform [min, max), clamped_normal, trapezoid.
class FloatProvider {
public:
    static FloatProvider constant(float v);
    static std::optional<FloatProvider> parse(const Json& j, FeatureLoader& loader);
    float sample(Xoroshiro& r) const;

private:
    enum class Kind : uint8_t { Constant, Uniform, ClampedNormal, Trapezoid };
    Kind kind_ = Kind::Constant;
    float a_ = 0, b_ = 0, c_ = 0, d_ = 0;
};

// {"absolute": y} | {"above_bottom": n} | {"below_top": n}
struct VerticalAnchor {
    enum class Kind : uint8_t { Absolute, AboveBottom, BelowTop };
    Kind kind = Kind::Absolute;
    int value = 0;
    int resolve() const;
    static std::optional<VerticalAnchor> parse(const Json& j, FeatureLoader& loader);
};

// HeightProvider: constant, uniform, trapezoid, biased_to_bottom, very_biased_to_bottom.
class HeightProvider {
public:
    static std::optional<HeightProvider> parse(const Json& j, FeatureLoader& loader);
    int sample(Xoroshiro& r) const;

private:
    enum class Kind : uint8_t { Constant, Uniform, Trapezoid, BiasedToBottom, VeryBiasedToBottom };
    Kind kind_ = Kind::Constant;
    VerticalAnchor min_, max_;
    int extra_ = 0; // plateau or inner
};

// BlockStateProvider: simple, weighted, noise, noise_threshold, dual_noise,
// rotated_block (random axis) and randomized_int_state (random property value).
class BlockStateProvider {
public:
    static BlockStateProvider simple(StateId s);
    static std::optional<BlockStateProvider> parse(const Json& j, FeatureLoader& loader);
    StateId get(Xoroshiro& r, const BlockPos& pos) const;

private:
    enum class Kind : uint8_t { Simple, Weighted, Noise, NoiseThreshold, DualNoise, Rotated, RandomizedInt };
    struct NoiseSettings {
        std::shared_ptr<NormalNoise> noise, slow;
        float scale = 1, slowScale = 1, threshold = 0, highChance = 0;
        IntProvider variety;
    };
    StateId pickByNoise(const std::vector<StateId>& states, double noise) const;
    double noiseAt(const NormalNoise& n, const BlockPos& p, double scale) const;

    Kind kind_ = Kind::Simple;
    StateId state_ = 0;
    const BlockRegistry* registry_ = nullptr;
    std::vector<std::pair<StateId, int>> weighted_;
    int totalWeight_ = 0;
    std::vector<StateId> states_, lowStates_, highStates_;
    std::shared_ptr<NoiseSettings> noise_;
    std::shared_ptr<BlockStateProvider> source_; // randomized_int_state
    std::string property_;
    IntProvider values_;
};

// BlockPredicate tree: matching_blocks, matching_block_tag, matching_fluids,
// solid, replaceable, would_survive, has_sturdy_face, inside_world_bounds,
// not, all_of, any_of, true.
class BlockPredicate {
public:
    static BlockPredicate alwaysTrue();
    static BlockPredicate matchingBlock(const Block& b, glm::ivec3 offset = {});
    static BlockPredicate air(glm::ivec3 offset = {});
    static std::optional<BlockPredicate> parse(const Json& j, FeatureLoader& loader);
    bool test(const WorldGenRegion& level, const BlockPos& pos) const;

private:
    enum class Kind : uint8_t {
        True, Air, MatchingBlocks, MatchingTag, MatchingFluids, Solid, Replaceable, WouldSurvive, Sturdy, InWorld, Not, AllOf,
        AnyOf
    };
    Kind kind_ = Kind::True;
    glm::ivec3 offset_{0};
    std::vector<uint16_t> blocks_; // block indices
    TagId tag_ = 0;
    std::vector<FluidKind> fluids_;
    StateId state_ = 0;
    Direction face_ = Direction::Up;
    std::vector<BlockPredicate> children_;
};

// Vanilla RuleTest for ore targets: tag_match, block_match, blockstate_match, always_true.
struct RuleTest {
    enum class Kind : uint8_t { Always, Tag, Block, State } kind = Kind::Always;
    TagId tag = 0;
    uint16_t block = 0;
    StateId state = 0;
    bool test(const BlockRegistry& reg, StateId s) const;
    static std::optional<RuleTest> parse(const Json& j, FeatureLoader& loader);
};

// Vanilla Biome.BIOME_INFO_NOISE (2D), used by noise-based counts and flower noise.
double biomeInfoNoise(double x, double z);

} // namespace mc
