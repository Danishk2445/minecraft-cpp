// BlockRegistry.h -- every block and block state, with per-state render and
// light data. Built from the pack's blockstate files plus data/blocks/*.json.
//
// States are numbered mixed-radix from each block's property schema (properties
// sorted by name, the last one varying fastest), so `with()` is O(1). StateIds
// change whenever a schema changes: never persist them (saves use describe()).
#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/Math.h"
#include "world/block/BlockTags.h"
#include "world/block/Tints.h"

namespace mc {

class Atlas;
class BlockModels;
class BlockRules;
struct BakedChoice;

using StateId = uint16_t;

struct Property {
    std::string name;
    std::vector<std::string> values;
    bool boolean = false; // values are {"false","true"}
    bool integer = false; // every value parses as an int
    int index(std::string_view value) const;
};

enum class RenderLayer : uint8_t { Solid, CutoutMipped, Cutout, Translucent, Invisible };

enum class FluidKind : uint8_t { None, Water, Lava };

struct FluidState {
    FluidKind kind = FluidKind::None;
    uint8_t level = 0;    // 0 = source, 1..7 flowing (vanilla block level & 7)
    bool falling = false; // block level >= 8
    bool empty() const { return kind == FluidKind::None; }
    bool source() const { return kind != FluidKind::None && level == 0 && !falling; }
};

// Per-state flags.
enum StateFlag : uint16_t {
    kAir = 1 << 0,
    kOpaqueCube = 1 << 1,   // solid layer, every face fully covered: culls neighbors, blocks all light
    kHasGeometry = 1 << 2,  // the model has at least one quad
    kWaterlogged = 1 << 3,  // carries water although its own model is something else
    kCodeRendered = 1 << 4, // drawn by code (fluids, block entities) instead of model quads
    kReplaceable = 1 << 5,  // placement and world generation may overwrite it (grass, water, snow layer)
    kSkipSame = 1 << 6,     // faces touching the same block are hidden (glass, ice, panes)
    kAoShade = 1 << 7,      // full-cube shape that darkens neighbors' ambient occlusion (not glass)
    kBlocksMotion = 1 << 8, // has collision and counts as solid (vanilla blocksMotion): heightmaps, worldgen
    kHasCollision = 1 << 9, // entities collide with it (not plants, fluids, signs, ...)
};

// A block shape: boxes in block-local coordinates (a full cube spans 0..1; fence
// and wall collision reaches 1.5 high).
using Shape = std::vector<AABB>;

struct StateInfo {
    uint16_t block = 0;
    uint16_t flags = 0;
    uint8_t emission = 0; // 0..15
    uint8_t opacity = 0;  // 0..15 light absorbed when entering this block
    RenderLayer layer = RenderLayer::Solid;
    uint8_t lightBlockingFaces = 0; // bit d: face d fully covered by an opaque shape (slab bottoms)
    uint8_t sturdyFaces = 0;        // bit d: face d fully covered by model faces of any opacity (supports torches, snow)
    uint16_t outline = 0;           // shape index: selection outline and ray targets
    uint16_t collision = 0;         // shape index: what entities collide with
    FluidState fluid;
    // 4x4 coverage of each face by opaque geometry (bit = row*4+col), for culling.
    std::array<uint16_t, 6> coverage{};
};

struct Block {
    std::string id; // "minecraft:oak_stairs"
    uint16_t index = 0;
    std::string className;
    std::vector<uint16_t> props;   // property ids, sorted by property name
    std::vector<uint32_t> strides; // mixed-radix stride per property slot
    StateId first = 0;
    uint32_t stateCount = 1;
    StateId defaultState = 0;
    TintRule tint;
    float friction = 0.6f;    // ground slipperiness (ice 0.98)
    float speedFactor = 1.0f; // walking on it (soul sand 0.4)
    float jumpFactor = 1.0f;  // jumping from it (honey 0.5)
    float hardness = 1.0f;    // vanilla destroyTime; negative = unbreakable
    bool requiresTool = false; // vanilla requiresCorrectToolForDrops: slow to break and no drops by hand

    StateId stateAt(uint32_t local) const { return StateId(first + local); }
};

class BlockRegistry {
public:
    // Builds blocks for every blockstate definition in `models` (whose quads
    // reference sprites in `atlas`), reading dataDir/blocks/classes.json and
    // dataDir/blocks/properties.json.
    bool build(const BlockModels& models, const Atlas& atlas, const std::filesystem::path& dataDir,
               std::string* error);

    size_t blockCount() const { return blocks_.size(); }
    size_t stateCount() const { return info_.size(); }
    const Block& block(size_t i) const { return blocks_[i]; }
    const Block& blockOf(StateId s) const { return blocks_[info_[s].block]; }
    const Block* find(std::string_view id) const; // namespace optional
    const Property& property(uint16_t id) const { return props_[id]; }

    const StateInfo& info(StateId s) const { return info_[s]; }
    std::span<const BakedChoice* const> modelChoices(StateId s) const;

    // Property access by name; empty/nullopt when the block lacks the property.
    std::string_view value(StateId s, std::string_view prop) const;
    int intValue(StateId s, std::string_view prop, int fallback = 0) const;
    bool boolValue(StateId s, std::string_view prop) const { return value(s, prop) == "true"; }
    std::optional<StateId> with(StateId s, std::string_view prop, std::string_view value) const;
    // Value index of property slot `slot` (index into Block::props).
    int slotValue(StateId s, size_t slot) const;
    StateId withSlot(StateId s, size_t slot, int valueIndex) const;

    // "minecraft:oak_stairs[facing=east,half=bottom,shape=straight,waterlogged=false]"
    std::string describe(StateId s) const;
    // Inverse of describe(); omitted properties take the default state's value.
    std::optional<StateId> parse(std::string_view text, std::string* error = nullptr) const;
    StateId defaultState(std::string_view blockId) const; // air if unknown

    static constexpr StateId kAirState = 0;

    // Block tags from dataDir/tags/blocks (loaded by build()).
    const BlockTags& tags() const { return tags_; }
    // Rules that look at neighboring blocks (canSurvive), built after the tags.
    const BlockRules& rules() const { return *rules_; }

    // Shapes (BlockShapes.cpp). Index 0 is empty, 1 the full cube.
    const Shape& outlineShape(StateId s) const { return shapes_[info_[s].outline]; }
    const Shape& collisionShape(StateId s) const { return shapes_[info_[s].collision]; }
    const Shape& shape(uint16_t index) const { return shapes_[index]; }
    size_t shapeCount() const { return shapes_.size(); }
    std::optional<TagId> tag(std::string_view name) const { return tags_.find(name); }
    bool hasTag(StateId s, TagId t) const { return tags_.containsBlock(t, info_[s].block); }

private:
    uint16_t internProperty(const std::string& name, const std::vector<std::string>& values);
    void buildShapes();

    std::vector<Block> blocks_;
    std::vector<Property> props_;
    std::unordered_map<std::string, uint16_t> blockIndex_;
    std::vector<StateInfo> info_;
    std::vector<uint32_t> choiceStart_; // per state, into choices_ (size stateCount + 1)
    std::vector<const BakedChoice*> choices_;
    BlockTags tags_;
    std::shared_ptr<const BlockRules> rules_;
    std::vector<Shape> shapes_;
};

// '*' wildcard match against a block id without namespace ("oak_stairs" ~ "*_stairs").
bool globMatch(std::string_view pattern, std::string_view text);

} // namespace mc
