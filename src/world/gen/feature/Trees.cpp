// Trees.cpp -- vanilla tree features: "tree" (TreeFeature with trunk placers,
// foliage placers, feature sizes and decorators), huge brown and red
// mushrooms, 1.21.5 fallen trees, and the root system that grows an azalea
// tree above a lush cave.
#include <array>
#include <climits>

#include "world/block/BlockRules.h"
#include "world/gen/feature/Feature.h"
#include "world/gen/feature/TreeConfig.h"

namespace mc {

std::string treeTypeOf(const Json& j) {
    std::string t = jsonGet<std::string>(j, "type", "");
    if (t.starts_with("minecraft:")) t.erase(0, 10);
    return t;
}

// ---------------------------------------------------------------- PosIndex

size_t PosIndex::slotOf(const BlockPos& p) const {
    uint64_t h = uint64_t(uint32_t(p.x)) * 0x9E3779B97F4A7C15ull;
    h ^= uint64_t(uint32_t(p.y)) * 0xC2B2AE3D27D4EB4Full;
    h ^= uint64_t(uint32_t(p.z)) * 0x165667B19E3779F9ull;
    h ^= h >> 29;
    return size_t(h) & (slots_.size() - 1);
}

int32_t PosIndex::find(const BlockPos& p) const {
    if (slots_.empty()) return -1;
    for (size_t i = slotOf(p);; i = (i + 1) & (slots_.size() - 1)) {
        const Slot& s = slots_[i];
        if (s.stamp != stamp_) return -1;
        if (s.pos == p) return s.value;
    }
}

int32_t PosIndex::insert(const BlockPos& p, int32_t value) {
    if ((count_ + 1) * 2 > slots_.size()) grow();
    for (size_t i = slotOf(p);; i = (i + 1) & (slots_.size() - 1)) {
        Slot& s = slots_[i];
        if (s.stamp != stamp_) {
            s = {p, stamp_, value};
            ++count_;
            return value;
        }
        if (s.pos == p) return s.value;
    }
}

void PosIndex::clear() {
    count_ = 0;
    if (++stamp_ == 0) { // wrapped: old stamps could look current
        for (Slot& s : slots_) s.stamp = 0;
        stamp_ = 1;
    }
}

void PosIndex::grow() {
    std::vector<Slot> old = std::move(slots_);
    slots_.assign(std::max<size_t>(256, old.size() * 2), Slot{});
    const uint32_t stamp = stamp_;
    stamp_ = 1;
    for (Slot& s : slots_) s.stamp = 0;
    count_ = 0;
    for (const Slot& s : old)
        if (s.stamp == stamp) insert(s.pos, s.value);
}

// ---------------------------------------------------------------- TreeBlocks

struct TreeBlocks::Scratch {
    struct Entry {
        BlockPos pos;
        StateId state;
        uint8_t roles;
    };
    PosIndex index;
    std::vector<Entry> entries;
    std::vector<BlockPos> logs, leaves;
    std::vector<uint8_t> best; // per box cell: distance found so far
    std::vector<uint32_t> touched;
    std::array<std::vector<uint32_t>, 7> buckets;
};

namespace {

// Reused per thread; a stack, in case a tree is built while another one is.
thread_local std::vector<std::unique_ptr<TreeBlocks::Scratch>> scratchPool;

constexpr uint8_t kUnreached = 0xFF;
constexpr std::string_view kDistance[] = {"0", "1", "2", "3", "4", "5", "6", "7"};

} // namespace

TreeBlocks::TreeBlocks(WorldGenRegion& level) : level(level) {
    if (scratchPool.empty()) {
        s_ = new Scratch();
    } else {
        s_ = scratchPool.back().release();
        scratchPool.pop_back();
    }
    s_->index.clear();
    s_->entries.clear();
    s_->logs.clear();
    s_->leaves.clear();
}

TreeBlocks::~TreeBlocks() { scratchPool.emplace_back(s_); }

StateId TreeBlocks::get(const BlockPos& p) const {
    const int32_t i = s_->index.find(p);
    return i >= 0 ? s_->entries[size_t(i)].state : level.getBlock(p);
}

void TreeBlocks::set(const BlockPos& p, StateId s, Role role) {
    const int32_t next = int32_t(s_->entries.size());
    const int32_t i = s_->index.insert(p, next);
    if (i == next) {
        s_->entries.push_back({p, s, uint8_t(role)});
    } else {
        s_->entries[size_t(i)].state = s;
        s_->entries[size_t(i)].roles |= uint8_t(role);
    }
}

bool TreeBlocks::has(const BlockPos& p, Role role) const {
    const int32_t i = s_->index.find(p);
    return i >= 0 && (s_->entries[size_t(i)].roles & role);
}

bool TreeBlocks::hasTreeBlocks() const {
    for (const auto& e : s_->entries)
        if (e.roles) return true;
    return false;
}

void TreeBlocks::updateLeaves(const FeatureTags& tags) {
    Scratch& s = *s_;
    // The search stays inside the tree's bounding box, as vanilla's does.
    int x0 = INT_MAX, y0 = INT_MAX, z0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN, z1 = INT_MIN;
    for (const auto& e : s.entries) {
        if (!e.roles) continue;
        x0 = std::min(x0, e.pos.x);
        y0 = std::min(y0, e.pos.y);
        z0 = std::min(z0, e.pos.z);
        x1 = std::max(x1, e.pos.x);
        y1 = std::max(y1, e.pos.y);
        z1 = std::max(z1, e.pos.z);
    }
    if (x0 > x1) return;
    const int sx = x1 - x0 + 1, sy = y1 - y0 + 1, sz = z1 - z0 + 1;
    s.best.assign(size_t(sx) * size_t(sy) * size_t(sz), kUnreached);
    s.touched.clear();
    for (auto& b : s.buckets) b.clear();
    auto cellOf = [&](const BlockPos& p) { return uint32_t(((p.y - y0) * sz + (p.z - z0)) * sx + (p.x - x0)); };
    auto posOf = [&](uint32_t c) {
        const int x = int(c % uint32_t(sx)), rest = int(c / uint32_t(sx));
        return BlockPos(x0 + x, y0 + rest / sz, z0 + rest % sz);
    };
    auto reach = [&](uint32_t c, int d) {
        if (s.best[c] == kUnreached) s.touched.push_back(c);
        s.best[c] = uint8_t(d);
        s.buckets[size_t(d)].push_back(c);
    };
    // Trunk blocks (with the dirt under them, like vanilla's trunk set) are distance 0.
    for (const auto& e : s.entries)
        if ((e.roles & kTrunk) && s.best[cellOf(e.pos)] != 0) reach(cellOf(e.pos), 0);

    const BlockRegistry& reg = level.registry();
    int k = 0;
    while (k < 7) {
        auto& bucket = s.buckets[size_t(k)];
        if (bucket.empty()) {
            ++k;
            continue;
        }
        const uint32_t c = bucket.back();
        bucket.pop_back();
        if (s.best[c] != k) continue; // reached by a shorter path since
        const BlockPos p = posOf(c);
        int lowest = k;
        for (Direction d : kDirections) {
            const BlockPos q = p.offset(d);
            if (q.x < x0 || q.x > x1 || q.y < y0 || q.y > y1 || q.z < z0 || q.z > z1) continue;
            const uint32_t qc = cellOf(q);
            if (s.best[qc] <= k + 1) continue;
            // Logs (other trees' too) are sources; other leaves keep a shorter distance they already have.
            const StateId st = get(q);
            int dist;
            if (reg.hasTag(st, tags.logs)) dist = 0;
            else if (reg.hasTag(st, tags.leaves)) dist = reg.intValue(st, "distance", 7);
            else continue;
            const int j = std::min(dist, k + 1);
            if (j >= 7 || j >= s.best[qc]) continue;
            reach(qc, j);
            lowest = std::min(lowest, j);
        }
        k = lowest;
    }
    for (uint32_t c : s.touched) {
        const int d = s.best[c];
        if (d == 0) continue;
        const BlockPos p = posOf(c);
        const StateId st = get(p);
        if (!reg.hasTag(st, tags.leaves) || reg.intValue(st, "distance", 7) == d) continue;
        set(p, withProp(reg, st, "distance", kDistance[d]), kOther);
    }
}

void TreeBlocks::flush(bool collect) {
    for (const auto& e : s_->entries) level.setBlock(e.pos, e.state, kWriteTree);
    if (!collect) return;
    for (const auto& e : s_->entries) {
        if (e.roles & kTrunk) s_->logs.push_back(e.pos);
        if (e.roles & kFoliage) s_->leaves.push_back(e.pos);
    }
    auto byY = [](const BlockPos& a, const BlockPos& b) { return a.y < b.y; };
    std::stable_sort(s_->logs.begin(), s_->logs.end(), byY);
    std::stable_sort(s_->leaves.begin(), s_->leaves.end(), byY);
}

std::span<const BlockPos> TreeBlocks::logs() const { return s_->logs; }
std::span<const BlockPos> TreeBlocks::leaves() const { return s_->leaves; }

// ---------------------------------------------------------------- TreeContext

bool TreeContext::validTreePos(const BlockPos& p) const {
    const StateId s = blocks.get(p);
    return (reg.info(s).flags & kAir) || reg.hasTag(s, tags.replaceableByTrees) || reg.hasTag(s, tags.leaves);
}

bool TreeContext::isFree(const BlockPos& p) const {
    const StateId s = blocks.get(p);
    return (reg.info(s).flags & kAir) || reg.hasTag(s, tags.replaceableByTrees) || reg.hasTag(s, tags.leaves) ||
           reg.hasTag(s, tags.logs);
}

bool TreeContext::isAirOrLeaves(const BlockPos& p) const {
    const StateId s = blocks.get(p);
    return (reg.info(s).flags & kAir) || reg.hasTag(s, tags.leaves);
}

bool TreeContext::placeLog(const BlockPos& p, int axis) {
    if (!validTreePos(p)) return false;
    static constexpr std::string_view axes[] = {"x", "y", "z"};
    StateId s = config.trunk.get(random, p);
    if (axis >= 0) s = withProp(reg, s, "axis", axes[axis]);
    blocks.set(p, s, TreeBlocks::kTrunk);
    return true;
}

void TreeContext::placeLogIfFree(const BlockPos& p) {
    if (isFree(p)) placeLog(p);
}

void TreeContext::setDirtAt(const BlockPos& p) {
    const StateId cur = blocks.get(p);
    const uint16_t b = reg.info(cur).block;
    const bool dirtLike = reg.hasTag(cur, tags.dirt) && b != tags.grassBlock && b != tags.mycelium;
    if (config.forceDirt || !dirtLike) blocks.set(p, config.dirt.get(random, p), TreeBlocks::kTrunk);
}

bool TreeContext::tryPlaceLeaf(const BlockPos& p) {
    const StateId cur = blocks.get(p);
    const StateInfo& ci = reg.info(cur);
    if (!(ci.flags & kAir) && !reg.hasTag(cur, tags.replaceableByTrees) && !reg.hasTag(cur, tags.leaves)) return false;
    if (reg.boolValue(cur, "persistent")) return false; // player-placed leaves stay
    StateId s = config.foliage.get(random, p);
    const bool water = ci.fluid.kind == FluidKind::Water && ci.fluid.source();
    if (water != ((reg.info(s).flags & kWaterlogged) != 0)) s = withProp(reg, s, "waterlogged", water);
    blocks.set(p, s, TreeBlocks::kFoliage);
    return true;
}

namespace {

Direction randomHorizontal(Xoroshiro& r) { return kHorizontal[size_t(r.nextInt(4))]; }

std::optional<BlockStateProvider> providerField(const Json& c, const char* key, FeatureLoader& l) {
    if (!c.contains(key)) {
        l.error(std::string("missing '") + key + "'");
        return std::nullopt;
    }
    return BlockStateProvider::parse(c[key], l);
}

// ---------------------------------------------------------------- tree

class TreeFeature final : public Feature {
public:
    explicit TreeFeature(const BlockRegistry& reg) : tags_(std::make_shared<FeatureTags>(reg)) {}

    TreeConfig config;

    bool place(FeatureContext& ctx) const override {
        Xoroshiro& r = ctx.random;
        const BlockPos pos = ctx.origin;
        const int height = config.trunkPlacer->treeHeight(r);
        const int foliageHeight = config.foliagePlacer->foliageHeight(r, height);
        const int foliageRadius = config.foliagePlacer->foliageRadius(r, height - foliageHeight);
        if (pos.y < kWorldMinY + 1 || pos.y + height + 1 > kWorldMaxY + 1) return false;
        const BlockRegistry& reg = ctx.level.registry();
        TreeBlocks blocks(ctx.level);
        TreeContext t{blocks, r, config, reg, *tags_};
        const int free = maxFreeHeight(t, height, pos);
        const auto& clipped = config.minimumSize.minClippedHeight;
        if (free < height && !(clipped && free >= *clipped)) return false;
        Attachments attachments;
        config.trunkPlacer->placeTrunk(t, free, pos, attachments);
        for (const FoliageAttachment& a : attachments)
            config.foliagePlacer->createFoliage(t, a, foliageHeight, foliageRadius);
        if (!blocks.hasTreeBlocks()) return false;
        blocks.updateLeaves(*tags_);
        // Decorators run on the written tree; none of them touches leaves, so
        // fixing distances first (vanilla does it last) gives the same result.
        blocks.flush(!config.decorators.empty());
        if (!config.decorators.empty()) {
            DecoratorContext dc{ctx.level, r, blocks.logs(), blocks.leaves(), {}};
            for (const auto& d : config.decorators) d->place(dc);
        }
        return true;
    }

private:
    // Vanilla getMaxFreeTreeHeight: the trunk length that fits, shortened by 2
    // below the first blocked layer.
    int maxFreeHeight(const TreeContext& t, int height, const BlockPos& pos) const {
        for (int y = 0; y <= height + 1; ++y) {
            const int size = config.minimumSize.sizeAtHeight(height, y);
            for (int dx = -size; dx <= size; ++dx)
                for (int dz = -size; dz <= size; ++dz) {
                    const BlockPos p = pos.offset(dx, y, dz);
                    if (!t.isFree(p)) return y - 2;
                    if (!config.ignoreVines && t.reg.info(t.blocks.get(p)).block == tags_->vine) return y - 2;
                }
        }
        return height;
    }

    std::shared_ptr<const FeatureTags> tags_;
};

std::unique_ptr<Feature> makeTree(const Json& c, FeatureLoader& l) {
    auto f = std::make_unique<TreeFeature>(l.registry);
    TreeConfig& t = f->config;
    if (c.contains("root_placer")) {
        l.error("root_placer is not supported");
        return nullptr;
    }
    auto trunk = providerField(c, "trunk_provider", l);
    auto foliage = providerField(c, "foliage_provider", l);
    auto dirt = BlockStateProvider::parse(c.value("dirt_provider", Json("dirt")), l);
    t.trunkPlacer = parseTrunkPlacer(c.value("trunk_placer", Json::object()), l);
    t.foliagePlacer = parseFoliagePlacer(c.value("foliage_placer", Json::object()), l);
    auto size = FeatureSize::parse(c.value("minimum_size", Json::object()), l);
    if (!trunk || !foliage || !dirt || !t.trunkPlacer || !t.foliagePlacer || !size) return nullptr;
    if (!parseTreeDecorators(c.value("decorators", Json::array()), l, t.decorators)) return nullptr;
    t.trunk = *trunk;
    t.foliage = *foliage;
    t.dirt = *dirt;
    t.minimumSize = *size;
    t.ignoreVines = l.getBool(c, "ignore_vines", false);
    t.forceDirt = l.getBool(c, "force_dirt", false);
    return f;
}

// ---------------------------------------------------------------- huge mushrooms

// Vanilla HugeBrownMushroomFeature / HugeRedMushroomFeature: a stem of 4-6
// blocks (doubled one time in 12) under a flat brown cap or a red dome. The
// cap's face properties mark which sides show the outer skin.
class HugeMushroomFeature final : public Feature {
public:
    HugeMushroomFeature(const BlockRegistry& reg, bool red)
        : red_(red), dirt_(reg.tag("dirt").value_or(TagId(0))), leaves_(reg.tag("leaves").value_or(TagId(0))),
          growBlock_(reg.tag("mushroom_grow_block").value_or(TagId(0))) {}

    BlockStateProvider cap = BlockStateProvider::simple(0), stem = BlockStateProvider::simple(0);
    int radius = 2;

    bool place(FeatureContext& ctx) const override {
        Xoroshiro& r = ctx.random;
        int height = r.nextInt(3) + 4;
        if (r.nextInt(12) == 0) height *= 2;
        if (!validPosition(ctx.level, ctx.origin, height)) return false;
        if (red_) redCap(ctx.level, r, ctx.origin, height);
        else brownCap(ctx.level, r, ctx.origin, height);
        for (int i = 0; i < height; ++i) {
            const BlockPos p = ctx.origin.above(i);
            if (!opaque(ctx.level, p)) ctx.level.setBlock(p, stem.get(r, ctx.origin), kWriteTree);
        }
        return true;
    }

private:
    static bool opaque(const WorldGenRegion& level, const BlockPos& p) { return level.info(p).flags & kOpaqueCube; }

    bool validPosition(const WorldGenRegion& level, const BlockPos& pos, int height) const {
        if (pos.y < kWorldMinY + 1 || pos.y + height + 1 > kWorldMaxY) return false;
        const StateId below = level.getBlock(pos.below());
        const BlockRegistry& reg = level.registry();
        if (!reg.hasTag(below, dirt_) && !reg.hasTag(below, growBlock_)) return false;
        for (int y = 0; y <= height; ++y) {
            // Vanilla checks the brown cap's full width above y 3, and only the stem column for red.
            const int size = !red_ && y > 3 ? radius : 0;
            for (int dx = -size; dx <= size; ++dx)
                for (int dz = -size; dz <= size; ++dz) {
                    const StateId s = level.getBlock(pos.offset(dx, y, dz));
                    if (!(reg.info(s).flags & kAir) && !reg.hasTag(s, leaves_)) return false;
                }
        }
        return true;
    }

    StateId faces(const BlockRegistry& reg, StateId s, bool west, bool east, bool north, bool south) const {
        if (reg.value(s, "west").empty()) return s;
        s = withProp(reg, s, "west", west);
        s = withProp(reg, s, "east", east);
        s = withProp(reg, s, "north", north);
        return withProp(reg, s, "south", south);
    }

    // One flat layer without corners; the outer skin shows on the rim.
    void brownCap(WorldGenRegion& level, Xoroshiro& r, const BlockPos& pos, int height) const {
        const BlockRegistry& reg = level.registry();
        const int n = radius;
        for (int dx = -n; dx <= n; ++dx)
            for (int dz = -n; dz <= n; ++dz) {
                const bool w = dx == -n, e = dx == n, no = dz == -n, so = dz == n;
                const bool xEdge = w || e, zEdge = no || so;
                if (xEdge && zEdge) continue;
                const BlockPos p = pos.offset(dx, height, dz);
                if (opaque(level, p)) continue;
                const StateId s = faces(reg, cap.get(r, pos), w || (zEdge && dx == 1 - n), e || (zEdge && dx == n - 1),
                                        no || (xEdge && dz == 1 - n), so || (xEdge && dz == n - 1));
                level.setBlock(p, s, kWriteTree);
            }
    }

    // Three rings of radius `radius` under a top of radius - 1.
    void redCap(WorldGenRegion& level, Xoroshiro& r, const BlockPos& pos, int height) const {
        const BlockRegistry& reg = level.registry();
        for (int y = height - 3; y <= height; ++y) {
            const int n = y < height ? radius : radius - 1;
            const int inner = radius - 2;
            for (int dx = -n; dx <= n; ++dx)
                for (int dz = -n; dz <= n; ++dz) {
                    const bool xEdge = dx == -n || dx == n, zEdge = dz == -n || dz == n;
                    if (y < height && xEdge == zEdge) continue;
                    const BlockPos p = pos.offset(dx, y, dz);
                    if (opaque(level, p)) continue;
                    StateId s = cap.get(r, pos);
                    if (!reg.value(s, "up").empty()) {
                        s = withProp(reg, s, "up", y >= height - 1);
                        s = faces(reg, s, dx < -inner, dx > inner, dz < -inner, dz > inner);
                    }
                    level.setBlock(p, s, kWriteTree);
                }
        }
    }

    bool red_;
    TagId dirt_, leaves_, growBlock_;
};

std::unique_ptr<Feature> makeHugeMushroom(const Json& c, FeatureLoader& l, bool red) {
    auto f = std::make_unique<HugeMushroomFeature>(l.registry, red);
    auto cap = providerField(c, "cap_provider", l);
    auto stem = providerField(c, "stem_provider", l);
    if (!cap || !stem) return nullptr;
    f->cap = *cap;
    f->stem = *stem;
    f->radius = l.getInt(c, "foliage_radius", 2);
    return f;
}

std::unique_ptr<Feature> makeHugeBrownMushroom(const Json& c, FeatureLoader& l) {
    return makeHugeMushroom(c, l, false);
}
std::unique_ptr<Feature> makeHugeRedMushroom(const Json& c, FeatureLoader& l) { return makeHugeMushroom(c, l, true); }

// ---------------------------------------------------------------- fallen tree

// 1.21.5 FallenTreeFeature: a one-block stump, and 1-2 blocks away a log lying
// on the ground (bridging gaps of at most 2), each with its own decorators.
class FallenTreeFeature final : public Feature {
public:
    explicit FallenTreeFeature(const BlockRegistry& reg) : tags_(std::make_shared<FeatureTags>(reg)) {}

    BlockStateProvider trunk = BlockStateProvider::simple(0);
    IntProvider logLength;
    std::vector<std::unique_ptr<TreeDecorator>> stumpDecorators, logDecorators;

    bool place(FeatureContext& ctx) const override {
        WorldGenRegion& level = ctx.level;
        Xoroshiro& r = ctx.random;
        const BlockPos stump = ctx.origin;
        level.setBlock(stump, trunk.get(r, stump), kWriteTree);
        decorate(level, r, {&stump, 1}, stumpDecorators);
        const Direction dir = randomHorizontal(r);
        const int length = logLength.sample(r) - 2;
        const int gap = 2 + r.nextInt(2);
        BlockPos p = ground(level, stump.offset(dir, gap));
        if (!fits(level, length, p, dir)) return true;
        std::vector<BlockPos> logs;
        static constexpr std::string_view axes[] = {"x", "y", "z"};
        for (int i = 0; i < length; ++i) {
            const StateId s = withProp(level.registry(), trunk.get(r, p), "axis", axes[int(axisOf(dir))]);
            level.setBlock(p, s, kWriteTree);
            logs.push_back(p);
            p = p.offset(dir);
        }
        decorate(level, r, logs, logDecorators);
        return true;
    }

private:
    static bool overSolidGround(const WorldGenRegion& level, const BlockPos& p) {
        return level.sturdy(p.below(), Direction::Up);
    }

    // From one above `p` down to 4 below: the first free spot on solid ground.
    BlockPos ground(const WorldGenRegion& level, BlockPos p) const {
        p = p.above();
        for (int i = 0; i < 6; ++i) {
            if (tags_->validTreePos(level, p) && overSolidGround(level, p)) return p;
            p = p.below();
        }
        return p;
    }

    bool fits(const WorldGenRegion& level, int length, BlockPos p, Direction dir) const {
        int unsupported = 0;
        for (int i = 0; i < length; ++i) {
            if (!tags_->validTreePos(level, p)) return false;
            if (!overSolidGround(level, p)) {
                if (++unsupported > 2) return false;
            } else {
                unsupported = 0;
            }
            p = p.offset(dir);
        }
        return true;
    }

    static void decorate(WorldGenRegion& level, Xoroshiro& r, std::span<const BlockPos> logs,
                         const std::vector<std::unique_ptr<TreeDecorator>>& decorators) {
        if (decorators.empty()) return;
        DecoratorContext dc{level, r, logs, {}, {}};
        for (const auto& d : decorators) d->place(dc);
    }

    std::shared_ptr<const FeatureTags> tags_;
};

std::unique_ptr<Feature> makeFallenTree(const Json& c, FeatureLoader& l) {
    auto f = std::make_unique<FallenTreeFeature>(l.registry);
    auto trunk = providerField(c, "trunk_provider", l);
    auto length = IntProvider::parse(c.value("log_length", Json()), l);
    if (!trunk || !length) return nullptr;
    if (!parseTreeDecorators(c.value("stump_decorators", Json::array()), l, f->stumpDecorators) ||
        !parseTreeDecorators(c.value("log_decorators", Json::array()), l, f->logDecorators))
        return nullptr;
    f->trunk = *trunk;
    f->logLength = *length;
    return f;
}

// ---------------------------------------------------------------- root system

// Vanilla RootSystemFeature (rooted azaleas): from a cave ceiling, climb to the
// first spot where the tree may grow (the surface above), grow it there, fill
// the column between with rooted dirt and hang roots from the ceiling.
class RootSystemFeature final : public Feature {
public:
    const PlacedFeature* tree = nullptr;
    int requiredVerticalSpace = 1, rootRadius = 3, rootAttempts = 20, rootColumnMaxHeight = 100;
    int hangingRadius = 3, hangingSpan = 2, hangingAttempts = 20, allowedVerticalWater = 0;
    TagId rootReplaceable = 0;
    BlockStateProvider rootState = BlockStateProvider::simple(0), hangingState = BlockStateProvider::simple(0);
    BlockPredicate allowedTreePosition;

    bool place(FeatureContext& ctx) const override {
        if (!ctx.level.isAir(ctx.origin)) return false;
        if (placeDirtAndTree(ctx)) placeRoots(ctx.level, ctx.random, ctx.origin);
        return true;
    }

private:
    bool placeDirtAndTree(FeatureContext& ctx) const {
        WorldGenRegion& level = ctx.level;
        BlockPos p = ctx.origin;
        for (int i = 0; i < rootColumnMaxHeight; ++i) {
            p = p.above();
            if (!allowedTreePosition.test(level, p) || !spaceForTree(level, p)) continue;
            const StateInfo& below = level.info(p.below());
            if (below.fluid.kind == FluidKind::Lava || !(below.flags & kBlocksMotion)) return false;
            PlacementContext nested{level};
            if (tree->place(nested, ctx.random, p)) {
                placeDirt(level, ctx.random, ctx.origin, ctx.origin.y + i);
                return true;
            }
        }
        return false;
    }

    bool spaceForTree(const WorldGenRegion& level, BlockPos p) const {
        for (int i = 1; i <= requiredVerticalSpace; ++i) {
            p = p.above();
            const StateInfo& s = level.info(p);
            if (s.flags & kAir) continue;
            if (i + 1 <= allowedVerticalWater && s.fluid.kind == FluidKind::Water) continue;
            return false;
        }
        return true;
    }

    void placeDirt(WorldGenRegion& level, Xoroshiro& r, const BlockPos& base, int maxY) const {
        for (int y = base.y; y < maxY; ++y)
            for (int j = 0; j < rootAttempts; ++j) {
                const int a = r.nextInt(rootRadius), b = r.nextInt(rootRadius);
                const int c = r.nextInt(rootRadius), d = r.nextInt(rootRadius);
                const BlockPos p(base.x + a - b, y, base.z + c - d);
                if (level.hasTag(p, rootReplaceable)) level.setBlock(p, rootState.get(r, p));
            }
    }

    void placeRoots(WorldGenRegion& level, Xoroshiro& r, const BlockPos& origin) const {
        const BlockRules& rules = level.registry().rules();
        for (int k = 0; k < hangingAttempts; ++k) {
            const int a = r.nextInt(hangingRadius), b = r.nextInt(hangingRadius);
            const int c = r.nextInt(hangingSpan), d = r.nextInt(hangingSpan);
            const int e = r.nextInt(hangingRadius), f = r.nextInt(hangingRadius);
            const BlockPos p = origin.offset(a - b, c - d, e - f);
            if (!level.isAir(p)) continue;
            const StateId s = hangingState.get(r, p);
            if (rules.canSurvive(level, s, p) && level.sturdy(p.above(), Direction::Down)) level.setBlock(p, s);
        }
    }
};

std::unique_ptr<Feature> makeRootSystem(const Json& c, FeatureLoader& l) {
    auto f = std::make_unique<RootSystemFeature>();
    f->tree = l.placed(c.value("feature", Json()));
    auto replaceable = l.tag(c.value("root_replaceable", Json()));
    auto root = providerField(c, "root_state_provider", l);
    auto hanging = providerField(c, "hanging_root_state_provider", l);
    auto allowed = BlockPredicate::parse(c.value("allowed_tree_position", Json{{"type", "true"}}), l);
    if (!f->tree || !replaceable || !root || !hanging || !allowed) return nullptr;
    f->rootReplaceable = *replaceable;
    f->rootState = *root;
    f->hangingState = *hanging;
    f->allowedTreePosition = std::move(*allowed);
    f->requiredVerticalSpace = l.getInt(c, "required_vertical_space_for_tree", 1);
    f->rootRadius = l.getInt(c, "root_radius", 3);
    f->rootAttempts = l.getInt(c, "root_placement_attempts", 20);
    f->rootColumnMaxHeight = l.getInt(c, "root_column_max_height", 100);
    f->hangingRadius = l.getInt(c, "hanging_root_radius", 3);
    f->hangingSpan = l.getInt(c, "hanging_roots_vertical_span", 2);
    f->hangingAttempts = l.getInt(c, "hanging_root_placement_attempts", 20);
    f->allowedVerticalWater = l.getInt(c, "allowed_vertical_water_for_tree", 0);
    if (f->rootRadius <= 0 || f->hangingRadius <= 0 || f->hangingSpan <= 0) {
        l.error("root and hanging root spreads must be positive");
        return nullptr;
    }
    return f;
}

} // namespace

void registerTreeFeatures(FeatureSet& set) {
    set.registerFeature("tree", makeTree);
    set.registerFeature("huge_brown_mushroom", makeHugeBrownMushroom);
    set.registerFeature("huge_red_mushroom", makeHugeRedMushroom);
    set.registerFeature("fallen_tree", makeFallenTree);
    set.registerFeature("root_system", makeRootSystem);
}

} // namespace mc
