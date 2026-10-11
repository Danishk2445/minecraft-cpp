// TreeConfig.h -- the parts of vanilla's TreeConfiguration (trunk placers,
// foliage placers, feature sizes, tree decorators) and the pending-block buffer
// a tree is built in. Shared by Trees.cpp (the features), TreePlacers.cpp and
// TreeDecorators.cpp.
#pragma once

#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "world/gen/feature/FeatureUtil.h"
#include "world/gen/feature/Providers.h"

namespace mc {

class FeatureLoader;
struct TreeConfig;

// Open addressing from block positions to small integers. clear() is O(1) (a
// generation stamp), so a table reused between trees stops allocating once it
// has grown to fit.
class PosIndex {
public:
    int32_t find(const BlockPos& p) const; // -1 if absent
    // The value stored for p, after storing `value` if p was absent.
    int32_t insert(const BlockPos& p, int32_t value);
    void clear();

private:
    struct Slot {
        BlockPos pos;
        uint32_t stamp = 0;
        int32_t value = 0;
    };
    size_t slotOf(const BlockPos& p) const;
    void grow();

    std::vector<Slot> slots_;
    uint32_t stamp_ = 1;
    size_t count_ = 0;
};

// A tree's own blocks before they reach the level. Vanilla writes each block as
// it goes and fixes leaf distances afterwards; keeping them here lets the
// distances be set first, so every block is written once. Reads see the pending
// blocks first, as vanilla's reads see its writes.
class TreeBlocks {
public:
    enum Role : uint8_t { kOther = 0, kTrunk = 1, kFoliage = 2 };

    explicit TreeBlocks(WorldGenRegion& level);
    ~TreeBlocks();
    TreeBlocks(const TreeBlocks&) = delete;
    TreeBlocks& operator=(const TreeBlocks&) = delete;

    WorldGenRegion& level;

    StateId get(const BlockPos& p) const;
    void set(const BlockPos& p, StateId s, Role role);
    bool has(const BlockPos& p, Role role) const;
    bool hasTreeBlocks() const; // any trunk or foliage
    // Vanilla TreeFeature.updateLeaves: leaf `distance` = steps to the nearest
    // log (trunk blocks count as logs, like vanilla's trunk set), at most 7.
    void updateLeaves(const FeatureTags& tags);
    // Writes everything (kWriteTree); with `collect`, also lists the trunk and
    // foliage positions sorted by y for decorators.
    void flush(bool collect);
    std::span<const BlockPos> logs() const;
    std::span<const BlockPos> leaves() const;

    struct Scratch; // per-thread storage reused between trees (Trees.cpp)

private:
    Scratch* s_;
};

struct FoliageAttachment {
    BlockPos pos;
    int radiusOffset = 0;
    bool doubleTrunk = false;
};
using Attachments = std::vector<FoliageAttachment>;

// What trunk and foliage placers work with (vanilla's level, setters and config).
struct TreeContext {
    TreeBlocks& blocks;
    Xoroshiro& random;
    const TreeConfig& config;
    const BlockRegistry& reg;
    const FeatureTags& tags;

    // Vanilla TreeFeature.validTreePos: air or replaceable_by_trees (which holds the leaves).
    bool validTreePos(const BlockPos& p) const;
    bool isFree(const BlockPos& p) const; // validTreePos or a log
    bool isAirOrLeaves(const BlockPos& p) const;
    // A trunk block if the spot is valid; `axis` 0/1/2 = x/y/z overrides the provider's.
    bool placeLog(const BlockPos& p, int axis = -1);
    void placeLogIfFree(const BlockPos& p);
    // Dirt under the trunk, unless already dirt-like (grass and mycelium are not).
    void setDirtAt(const BlockPos& p);
    bool tryPlaceLeaf(const BlockPos& p);
    bool foliageAt(const BlockPos& p) const { return blocks.has(p, TreeBlocks::kFoliage); }
};

// two_layers_feature_size / three_layers_feature_size: the free radius a trunk
// needs at each height (vanilla TreeFeature.getMaxFreeTreeHeight).
struct FeatureSize {
    bool threeLayers = false;
    int limit = 1, upperLimit = 1, lowerSize = 0, middleSize = 1, upperSize = 1;
    std::optional<int> minClippedHeight;

    int sizeAtHeight(int height, int y) const {
        if (y < limit) return lowerSize;
        if (!threeLayers) return upperSize;
        return y >= height - upperLimit ? upperSize : middleSize;
    }
    static std::optional<FeatureSize> parse(const Json& j, FeatureLoader& l);
};

class TrunkPlacer {
public:
    virtual ~TrunkPlacer() = default;
    int treeHeight(Xoroshiro& r) const {
        const int a = r.nextInt(randA + 1);
        return baseHeight + a + r.nextInt(randB + 1);
    }
    // Places the trunk (`height` blocks free above `pos`) and appends where foliage goes.
    virtual void placeTrunk(TreeContext& t, int height, const BlockPos& pos, Attachments& out) const = 0;

    int baseHeight = 0, randA = 0, randB = 0;
};

class FoliagePlacer {
public:
    virtual ~FoliagePlacer() = default;
    virtual int foliageHeight(Xoroshiro& r, int treeHeight) const = 0;
    virtual int foliageRadius(Xoroshiro& r, int trunkHeight) const { return radius.sample(r); }
    void createFoliage(TreeContext& t, const FoliageAttachment& a, int foliageHeight, int foliageRadius) const {
        place(t, a, foliageHeight, foliageRadius, offset.sample(t.random));
    }

    IntProvider radius, offset;

protected:
    virtual void place(TreeContext& t, const FoliageAttachment& a, int height, int radius, int offset) const = 0;
    // Vanilla shouldSkipLocation, on distances from the center column.
    virtual bool skip(Xoroshiro& r, int dx, int y, int dz, int range, bool large) const = 0;
    virtual bool skipSigned(Xoroshiro& r, int dx, int y, int dz, int range, bool large) const;
    void placeLeavesRow(TreeContext& t, const BlockPos& center, int range, int y, bool large) const;
    void placeLeavesRowWithHangingLeavesBelow(TreeContext& t, const BlockPos& center, int range, int y, bool large,
                                              float chance, float extensionChance) const;
};

// What decorators see: the level after the tree is written, and the tree's
// blocks sorted by y. Fallen trees reuse decorators with their own log list.
struct DecoratorContext {
    WorldGenRegion& level;
    Xoroshiro& random;
    std::span<const BlockPos> logs, leaves, roots;

    bool isAir(const BlockPos& p) const { return level.isAir(p); }
    void setBlock(const BlockPos& p, StateId s) const { level.setBlock(p, s, kWriteTree); }
};

class TreeDecorator {
public:
    virtual ~TreeDecorator() = default;
    virtual void place(DecoratorContext& c) const = 0;
};

struct TreeConfig {
    BlockStateProvider trunk = BlockStateProvider::simple(0);
    BlockStateProvider foliage = BlockStateProvider::simple(0);
    BlockStateProvider dirt = BlockStateProvider::simple(0);
    std::unique_ptr<TrunkPlacer> trunkPlacer;
    std::unique_ptr<FoliagePlacer> foliagePlacer;
    FeatureSize minimumSize;
    std::vector<std::unique_ptr<TreeDecorator>> decorators;
    bool ignoreVines = false;
    bool forceDirt = false;
};

std::unique_ptr<TrunkPlacer> parseTrunkPlacer(const Json& j, FeatureLoader& l);
std::unique_ptr<FoliagePlacer> parseFoliagePlacer(const Json& j, FeatureLoader& l);
// A JSON list of decorators; false (with an error reported) if any is invalid.
bool parseTreeDecorators(const Json& j, FeatureLoader& l, std::vector<std::unique_ptr<TreeDecorator>>& out);
// A type name without the "minecraft:" namespace.
std::string treeTypeOf(const Json& j);

} // namespace mc
