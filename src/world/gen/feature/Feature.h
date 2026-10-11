// Feature.h -- configured and placed features (vanilla worldgen data model).
//
// A Feature places blocks around an origin (a tree, an ore blob, a flower
// patch). A PlacedFeature runs a chain of placement modifiers (count, in_square,
// height_range, heightmap, biome, ...) that turn one origin into the positions
// where the feature is tried, depth first like vanilla's lazy streams.
// Definitions live in data/worldgen/features/*.json in vanilla's JSON shape.
#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Json.h"
#include "core/Rng.h"
#include "world/gen/feature/WorldGenRegion.h"

namespace mc {

class FeatureLoader;
class PlacedFeature;

// What a placement chain runs against. `step`/`feature` identify the top-level
// feature being decorated, for the biome filter; `biomeHas` answers it.
struct PlacementContext {
    WorldGenRegion& level;
    const class BiomeDecorator* decorator = nullptr; // null for nested or live-world placement
    uint8_t step = 0;
    uint16_t feature = 0;
};

struct FeatureContext {
    WorldGenRegion& level;
    Xoroshiro& random;
    BlockPos origin;
    PlacementContext& placement; // for nested placed features
};

class Feature {
public:
    virtual ~Feature() = default;
    virtual bool place(FeatureContext& ctx) const = 0;
};

class PlacementModifier {
public:
    virtual ~PlacementModifier() = default;
    // Appends the positions that `pos` turns into (none filters it out).
    virtual void apply(PlacementContext& ctx, Xoroshiro& random, const BlockPos& pos, std::vector<BlockPos>& out) const = 0;
};

class PlacedFeature {
public:
    std::string name;
    const Feature* feature = nullptr;
    std::vector<std::unique_ptr<PlacementModifier>> placement;

    // Runs the placement chain from `origin`, then the feature at each position.
    bool place(PlacementContext& ctx, Xoroshiro& random, const BlockPos& origin) const;

private:
    bool placeFrom(size_t modifier, PlacementContext& ctx, Xoroshiro& random, const BlockPos& pos) const;
};

// Reads feature JSON: block states ("oak_log[axis=y]" or vanilla {"Name", "Properties"}),
// tags, and nested features (a name, or an inline {"type", "config", "placement"}).
class FeatureLoader {
public:
    FeatureLoader(const BlockRegistry& registry, class FeatureSet& set) : registry(registry), set_(set) {}

    const BlockRegistry& registry;
    std::string where; // the definition being read, for messages

    void error(const std::string& message);
    std::optional<StateId> state(const Json& j);
    std::optional<TagId> tag(const Json& j);
    const Block* block(const Json& j);
    // A placed feature: a name (resolved against all loaded definitions) or an inline definition.
    const PlacedFeature* placed(const Json& j);
    // Typed fields with error reporting.
    int getInt(const Json& j, const char* key, int fallback);
    float getFloat(const Json& j, const char* key, float fallback);
    bool getBool(const Json& j, const char* key, bool fallback);

private:
    FeatureSet& set_;
};

using FeatureFactory = std::unique_ptr<Feature> (*)(const Json& config, FeatureLoader& loader);
using ModifierFactory = std::unique_ptr<PlacementModifier> (*)(const Json& json, FeatureLoader& loader);

// Every feature and placed feature, by name.
class FeatureSet {
public:
    FeatureSet();
    ~FeatureSet();
    // Loads every *.json under `dir`; each maps names to {"type", "config", "placement"}.
    bool load(const std::filesystem::path& dir, const BlockRegistry& registry, std::string* errors);
    const PlacedFeature* find(std::string_view name) const;
    size_t size() const { return placed_.size(); }

    void registerFeature(std::string type, FeatureFactory f) { featureTypes_[std::move(type)] = f; }
    void registerModifier(std::string type, ModifierFactory f) { modifierTypes_[std::move(type)] = f; }

private:
    friend class FeatureLoader;
    const PlacedFeature* build(const std::string& name, const Json& def, FeatureLoader& loader);

    std::unordered_map<std::string, FeatureFactory> featureTypes_;
    std::unordered_map<std::string, ModifierFactory> modifierTypes_;
    std::unordered_map<std::string, Json> definitions_;
    std::unordered_map<std::string, const PlacedFeature*> byName_;
    std::vector<std::string> building_; // names under construction (cycle guard)
    std::vector<std::string> failed_;   // names that did not build (reported once)
    std::vector<std::unique_ptr<Feature>> features_;
    std::vector<std::unique_ptr<PlacedFeature>> placed_;
    std::vector<std::string> errors_;
};

// Feature families, each in its own source file.
void registerPlacementModifiers(FeatureSet& set);
void registerBasicFeatures(FeatureSet& set);     // ore, disk, simple_block, random_patch, selectors, block_column...
void registerTreeFeatures(FeatureSet& set);      // tree, huge mushrooms, fallen trees
void registerVegetationFeatures(FeatureSet& set); // water plants, coral, bamboo, vines, surface extras
void registerCaveFeatures(FeatureSet& set);      // lush and dripstone caves, springs, lakes, glow lichen, ice

} // namespace mc
