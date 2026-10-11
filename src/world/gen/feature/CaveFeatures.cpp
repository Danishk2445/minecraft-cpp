// CaveFeatures.cpp -- vanilla cave and underground features: lake (lava lakes),
// spring_feature, underwater_magma, multiface_growth (glow lichen),
// vegetation_patch and waterlogged_vegetation_patch (lush cave moss and clay
// pools) and random_boolean_selector. Dripstone is in CaveDripstone.cpp.
#include <algorithm>
#include <array>

#include "world/gen/feature/CaveCommon.h"
#include "world/gen/feature/FeatureUtil.h"
#include "world/gen/feature/Providers.h"

namespace mc {
namespace {

uint16_t blockIndex(const BlockRegistry& reg, std::string_view id) {
    const Block* b = reg.find(id);
    return b ? b->index : uint16_t(0xFFFF);
}

// The block's property slot named `name`, or -1.
int propertySlot(const BlockRegistry& reg, const Block& b, std::string_view name) {
    for (size_t i = 0; i < b.props.size(); ++i)
        if (reg.property(b.props[i]).name == name) return int(i);
    return -1;
}

// Vanilla Util.shuffle over the first n entries.
template <size_t N>
void utilShuffle(std::array<Direction, N>& a, int n, Xoroshiro& r) {
    for (int j = n; j > 1; --j) std::swap(a[size_t(j - 1)], a[size_t(r.nextInt(j))]);
}

// A block set from JSON (vanilla HolderSet<Block>): an id, a list of ids or a "#tag".
class BlockSet {
public:
    bool contains(uint16_t block) const {
        return size_t(block >> 6) < bits_.size() && ((bits_[block >> 6] >> (block & 63)) & 1u);
    }

    static std::optional<BlockSet> parse(const Json& j, FeatureLoader& l) {
        BlockSet s;
        s.bits_.assign((l.registry.blockCount() + 63) / 64, 0);
        auto add = [&](uint16_t b) { s.bits_[b >> 6] |= 1ull << (b & 63); };
        if (j.is_string() && j.get<std::string>().starts_with('#')) {
            auto t = l.tag(j);
            if (!t) return std::nullopt;
            for (uint16_t b : l.registry.tags().blocks(*t)) add(b);
            return s;
        }
        for (const Json& e : j.is_array() ? j : Json::array({j})) {
            const Block* b = l.block(e);
            if (!b) return std::nullopt;
            add(b->index);
        }
        return s;
    }

private:
    std::vector<uint64_t> bits_;
};

// ---------------------------------------------------------------- lake

// Vanilla LakeFeature (only lava lakes since 1.18): a blob of 4-7 ellipsoids in a
// 16x8x16 box, fluid in its lower half and cave air above. Gives up unless the
// blob's shell is solid (or the fluid) below the fluid line and holds no liquid
// above it; then turns solid shell blocks into the barrier.
class LakeFeature final : public Feature {
public:
    BlockStateProvider fluid = BlockStateProvider::simple(0), barrier = BlockStateProvider::simple(0);
    StateId caveAir = 0;
    uint16_t water = 0, lava = 0;
    TagId cannotReplace = 0, leaves = 0, logs = 0;

    bool place(FeatureContext& ctx) const override {
        WorldGenRegion& level = ctx.level;
        const BlockRegistry& reg = level.registry();
        Xoroshiro& r = ctx.random;
        if (ctx.origin.y <= kWorldMinY + 4) return false;
        const BlockPos o = ctx.origin.below(4);
        std::array<bool, 16 * 16 * 8> lake{}; // (x * 16 + z) * 8 + y
        const int blobs = r.nextInt(4) + 4;
        for (int i = 0; i < blobs; ++i) {
            const double sx = r.nextDouble() * 6.0 + 3.0, sy = r.nextDouble() * 4.0 + 2.0;
            const double sz = r.nextDouble() * 6.0 + 3.0;
            const double cx = r.nextDouble() * (16.0 - sx - 2.0) + 1.0 + sx / 2.0;
            const double cy = r.nextDouble() * (8.0 - sy - 4.0) + 2.0 + sy / 2.0;
            const double cz = r.nextDouble() * (16.0 - sz - 2.0) + 1.0 + sz / 2.0;
            for (int x = 1; x < 15; ++x)
                for (int z = 1; z < 15; ++z)
                    for (int y = 1; y < 7; ++y) {
                        const double dx = (x - cx) / (sx / 2.0), dy = (y - cy) / (sy / 2.0), dz = (z - cz) / (sz / 2.0);
                        if (dx * dx + dy * dy + dz * dz < 1.0) lake[size_t((x * 16 + z) * 8 + y)] = true;
                    }
        }
        auto in = [&](int x, int y, int z) { return lake[size_t((x * 16 + z) * 8 + y)]; };
        auto shell = [&](int x, int y, int z) {
            return !in(x, y, z) && ((x < 15 && in(x + 1, y, z)) || (x > 0 && in(x - 1, y, z)) ||
                                    (z < 15 && in(x, y, z + 1)) || (z > 0 && in(x, y, z - 1)) ||
                                    (y < 7 && in(x, y + 1, z)) || (y > 0 && in(x, y - 1, z)));
        };
        const StateId fluidState = fluid.get(r, o);
        for (int x = 0; x < 16; ++x)
            for (int z = 0; z < 16; ++z)
                for (int y = 0; y < 8; ++y) {
                    if (!shell(x, y, z)) continue;
                    const StateId s = level.getBlock(o.offset(x, y, z));
                    const StateInfo& si = reg.info(s);
                    if (y >= 4 && (si.block == water || si.block == lava)) return false;
                    if (y < 4 && !(si.flags & kBlocksMotion) && s != fluidState) return false;
                }
        for (int x = 0; x < 16; ++x)
            for (int z = 0; z < 16; ++z)
                for (int y = 0; y < 8; ++y) {
                    const BlockPos p = o.offset(x, y, z);
                    if (in(x, y, z) && !level.hasTag(p, cannotReplace))
                        level.setBlock(p, y >= 4 ? caveAir : fluidState);
                }
        const StateId wall = barrier.get(r, o);
        if (!(reg.info(wall).flags & kAir))
            for (int x = 0; x < 16; ++x)
                for (int z = 0; z < 16; ++z)
                    for (int y = 0; y < 8; ++y) {
                        if (!shell(x, y, z) || (y >= 4 && r.nextInt(2) == 0)) continue;
                        const BlockPos p = o.offset(x, y, z);
                        const StateId s = level.getBlock(p);
                        // Vanilla's lava_pool_stone_cannot_replace: features_cannot_replace, leaves and logs.
                        const bool kept = reg.hasTag(s, cannotReplace) || reg.hasTag(s, leaves) || reg.hasTag(s, logs);
                        if ((reg.info(s).flags & kBlocksMotion) && !kept) level.setBlock(p, wall);
                    }
        // Vanilla also freezes water lakes, but tests the cave air above the water,
        // so that never happens.
        return true;
    }
};

std::unique_ptr<Feature> makeLake(const Json& c, FeatureLoader& l) {
    auto fluid = BlockStateProvider::parse(c.value("fluid", Json()), l);
    auto barrier = BlockStateProvider::parse(c.value("barrier", Json()), l);
    if (!fluid || !barrier) return nullptr;
    const BlockRegistry& reg = l.registry;
    const FeatureTags& tags = featureTags(reg);
    auto f = std::make_unique<LakeFeature>();
    f->fluid = *fluid;
    f->barrier = *barrier;
    const Block* caveAir = reg.find("cave_air");
    f->caveAir = caveAir ? caveAir->defaultState : BlockRegistry::kAirState;
    f->water = blockIndex(reg, "water");
    f->lava = blockIndex(reg, "lava");
    f->cannotReplace = tags.featuresCannotReplace;
    f->leaves = tags.leaves;
    f->logs = tags.logs;
    return f;
}

// ---------------------------------------------------------------- spring_feature

// Vanilla SpringFeature: a fluid source in a wall, with exactly `rock_count`
// valid blocks and `hole_count` air blocks among its four sides and the block below.
class SpringFeature final : public Feature {
public:
    StateId state = 0;
    bool requiresBlockBelow = true;
    int rockCount = 4, holeCount = 1;
    BlockSet valid;

    bool place(FeatureContext& ctx) const override {
        const WorldGenRegion& level = ctx.level;
        const BlockPos p = ctx.origin;
        auto isValid = [&](const BlockPos& q) { return valid.contains(level.info(q).block); };
        if (!isValid(p.above()) || (requiresBlockBelow && !isValid(p.below()))) return false;
        if (!level.isAir(p) && !isValid(p)) return false;
        int rock = 0, hole = 0;
        for (const BlockPos& q : {p.offset(Direction::West), p.offset(Direction::East), p.offset(Direction::North),
                                  p.offset(Direction::South), p.below()}) {
            rock += isValid(q);
            hole += level.isAir(q);
        }
        // Without fluid ticks the source stays a single block until something updates it.
        return rock == rockCount && hole == holeCount && ctx.level.setBlock(p, state);
    }
};

std::unique_ptr<Feature> makeSpring(const Json& c, FeatureLoader& l) {
    // The fluid state ({"Name": "minecraft:lava", "Properties": {"falling": "true"}}) becomes
    // its source block, as vanilla's createLegacyBlock does for the sources it uses.
    const Json s = c.value("state", Json());
    std::string name = s.is_string() ? s.get<std::string>() : jsonGet<std::string>(s, "Name", "");
    if (name.starts_with("minecraft:")) name.erase(0, 10);
    if (name.starts_with("flowing_")) name.erase(0, 8);
    const Block* fluid = name == "water" || name == "lava" ? l.registry.find(name) : nullptr;
    auto valid = BlockSet::parse(c.value("valid_blocks", Json::array()), l);
    if (!fluid) l.error("spring state should be water or lava");
    if (!fluid || !valid) return nullptr;
    auto f = std::make_unique<SpringFeature>();
    f->state = fluid->defaultState;
    f->requiresBlockBelow = l.getBool(c, "requires_block_below", true);
    f->rockCount = l.getInt(c, "rock_count", 4);
    f->holeCount = l.getInt(c, "hole_count", 1);
    f->valid = std::move(*valid);
    return f;
}

// ---------------------------------------------------------------- underwater_magma

// Vanilla UnderwaterMagmaFeature: from a water block, finds the floor within
// `floor_search_range` and turns floor blocks around it that are buried on four
// sides and below into magma.
class UnderwaterMagmaFeature final : public Feature {
public:
    int searchRange = 5, radius = 1;
    float chance = 0.5f;
    uint16_t water = 0;
    StateId magma = 0;

    bool place(FeatureContext& ctx) const override {
        WorldGenRegion& level = ctx.level;
        const BlockRegistry& reg = level.registry();
        auto isWater = [&](StateId s) { return reg.info(s).block == water; };
        const auto col = scanColumn(level, ctx.origin, searchRange, isWater, [&](StateId s) { return !isWater(s); });
        if (!col || !col->floor) return false;
        const BlockPos floor(ctx.origin.x, *col->floor, ctx.origin.z);
        auto open = [&](const BlockPos& p) {
            const StateInfo& i = level.info(p);
            return i.block == water || (i.flags & kAir);
        };
        int placed = 0;
        // BlockPos.betweenClosed order: x fastest, then y, then z.
        for (int dz = -radius; dz <= radius; ++dz)
            for (int dy = -radius; dy <= radius; ++dy)
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (!(ctx.random.nextFloat() < chance)) continue;
                    const BlockPos p = floor.offset(dx, dy, dz);
                    if (open(p) || open(p.below())) continue;
                    bool buried = true;
                    for (Direction d : kHorizontal) buried = buried && !open(p.offset(d));
                    if (buried && level.setBlock(p, magma)) ++placed;
                }
        return placed > 0;
    }
};

std::unique_ptr<Feature> makeUnderwaterMagma(const Json& c, FeatureLoader& l) {
    auto f = std::make_unique<UnderwaterMagmaFeature>();
    f->searchRange = l.getInt(c, "floor_search_range", 5);
    f->radius = l.getInt(c, "placement_radius_around_floor", 1);
    f->chance = l.getFloat(c, "placement_probability_per_valid_position", 0.5f);
    f->water = blockIndex(l.registry, "water");
    f->magma = l.registry.defaultState("magma_block");
    return f;
}

// ---------------------------------------------------------------- multiface_growth

// Vanilla MultifaceGrowthFeature (glow lichen): covers one face toward a nearby
// `can_be_placed_on` block, then may spread once to a neighboring face
// (MultifaceSpreader with the default spread config).
class MultifaceGrowthFeature final : public Feature {
public:
    using Dirs = std::array<Direction, 6>;
    uint16_t block = 0, water = 0;
    StateId bare = 0;                 // no faces, not waterlogged
    std::array<int8_t, 6> faceSlot{}; // property slot of each face, by Direction
    int waterloggedSlot = -1;
    int searchRange = 10;
    float chanceOfSpreading = 0.5f;
    Dirs valid{}; // vanilla order: up (ceiling), down (floor), north, east, south, west (walls)
    int validCount = 0;
    BlockSet placeOn;

    bool place(FeatureContext& ctx) const override {
        WorldGenRegion& level = ctx.level;
        Xoroshiro& r = ctx.random;
        const BlockPos o = ctx.origin;
        const StateId here = level.getBlock(o);
        if (!airOrWater(level.registry(), here)) return false;
        Dirs dirs = valid;
        utilShuffle(dirs, validCount, r);
        if (grow(level, o, here, dirs, validCount, r)) return true;
        for (int i = 0; i < validCount; ++i) {
            const Direction d = dirs[size_t(i)];
            Dirs others{};
            int n = 0;
            for (int k = 0; k < validCount; ++k)
                if (valid[size_t(k)] != opposite(d)) others[size_t(n++)] = valid[size_t(k)];
            utilShuffle(others, n, r);
            // Vanilla's search loop runs `search_range` times, but every step re-tests
            // origin + d (it offsets from the origin, not from the last step) and a failed
            // test changes nothing, so one test does the same.
            if (searchRange <= 0) continue;
            const BlockPos p = o.offset(d);
            const StateId s = level.getBlock(p);
            if (!airOrWater(level.registry(), s) && level.registry().info(s).block != block) continue;
            if (grow(level, p, s, others, n, r)) return true;
        }
        return false;
    }

private:
    bool airOrWater(const BlockRegistry& reg, StateId s) const {
        const StateInfo& i = reg.info(s);
        return (i.flags & kAir) || i.block == water;
    }
    bool hasFace(const BlockRegistry& reg, StateId s, Direction d) const {
        return reg.info(s).block == block && reg.slotValue(s, size_t(faceSlot[size_t(d)])) == 1;
    }

    // Vanilla MultifaceBlock.getStateForPlacement: `cur` (or a fresh block) with a
    // face toward d, if it lacks one there and the neighbor's facing side is sturdy.
    std::optional<StateId> withNewFace(const WorldGenRegion& level, StateId cur, const BlockPos& p, Direction d) const {
        const BlockRegistry& reg = level.registry();
        const StateInfo& i = reg.info(cur);
        const bool same = i.block == block;
        if ((same && hasFace(reg, cur, d)) || !level.sturdy(p.offset(d), opposite(d))) return std::nullopt;
        StateId s = same ? cur : bare;
        if (!same && waterloggedSlot >= 0 && i.fluid.kind == FluidKind::Water && i.fluid.source())
            s = reg.withSlot(s, size_t(waterloggedSlot), 1);
        return reg.withSlot(s, size_t(faceSlot[size_t(d)]), 1);
    }

    // Vanilla placeGrowthIfPossible: the first listed direction with a placeable neighbor decides.
    bool grow(WorldGenRegion& level, const BlockPos& p, StateId cur, const Dirs& dirs, int n, Xoroshiro& r) const {
        for (int i = 0; i < n; ++i) {
            const Direction d = dirs[size_t(i)];
            if (!placeOn.contains(level.info(p.offset(d)).block)) continue;
            const auto s = withNewFace(level, cur, p, d);
            if (!s) return false;
            level.setBlock(p, *s);
            if (r.nextFloat() < chanceOfSpreading) spread(level, *s, p, d, r);
            return true;
        }
        return false;
    }

    // Vanilla MultifaceSpreader.spreadFromFaceTowardRandomDirection: for each
    // shuffled direction across the face, the first spot that can take a face (this
    // block, the neighbor on the same wall, or around the wall's edge) gets one.
    void spread(WorldGenRegion& level, StateId s, const BlockPos& p, Direction from, Xoroshiro& r) const {
        const BlockRegistry& reg = level.registry();
        Dirs all = kDirections;
        utilShuffle(all, 6, r);
        for (Direction d : all) {
            if (axisOf(d) == axisOf(from) || !hasFace(reg, s, from) || hasFace(reg, s, d)) continue;
            const std::array<std::pair<BlockPos, Direction>, 3> spots = {
                {{p, d}, {p.offset(d), from}, {p.offset(d).offset(from), opposite(d)}}};
            for (const auto& [q, face] : spots) {
                const StateId cur = level.getBlock(q);
                const StateInfo& i = reg.info(cur);
                if (!(i.flags & kAir) && i.block != block && !(i.block == water && i.fluid.source())) continue;
                const auto next = withNewFace(level, cur, q, face);
                if (!next) continue;
                if (level.setBlock(q, *next)) return;
                break;
            }
        }
    }
};

std::unique_ptr<Feature> makeMultifaceGrowth(const Json& c, FeatureLoader& l) {
    const Block* b = l.block(c.value("block", Json("glow_lichen")));
    auto placeOn = BlockSet::parse(c.value("can_be_placed_on", Json::array()), l);
    if (!b || !placeOn) return nullptr;
    const BlockRegistry& reg = l.registry;
    auto f = std::make_unique<MultifaceGrowthFeature>();
    f->block = b->index;
    f->water = blockIndex(reg, "water");
    for (Direction d : kDirections) {
        const int slot = propertySlot(reg, *b, directionName(d));
        if (slot < 0) {
            l.error(b->id + " is not a multiface block");
            return nullptr;
        }
        f->faceSlot[size_t(d)] = int8_t(slot);
    }
    f->waterloggedSlot = propertySlot(reg, *b, "waterlogged");
    StateId bare = b->defaultState;
    for (Direction d : kDirections) bare = reg.withSlot(bare, size_t(f->faceSlot[size_t(d)]), 0);
    if (f->waterloggedSlot >= 0) bare = reg.withSlot(bare, size_t(f->waterloggedSlot), 0);
    f->bare = bare;
    f->searchRange = l.getInt(c, "search_range", 10);
    f->chanceOfSpreading = l.getFloat(c, "chance_of_spreading", 0.5f);
    if (l.getBool(c, "can_place_on_ceiling", false)) f->valid[size_t(f->validCount++)] = Direction::Up;
    if (l.getBool(c, "can_place_on_floor", false)) f->valid[size_t(f->validCount++)] = Direction::Down;
    if (l.getBool(c, "can_place_on_wall", false))
        for (Direction d : kHorizontal) f->valid[size_t(f->validCount++)] = d;
    f->placeOn = std::move(*placeOn);
    return f;
}

// ---------------------------------------------------------------- vegetation_patch

// Vanilla VegetationPatchFeature: turns the floor (or ceiling) of a cave under a
// roughly square area into `ground_state`, then grows `vegetation_feature` on part
// of it. The waterlogged variant (WaterloggedVegetationPatchFeature) floods the
// enclosed part of the patch and grows the vegetation in that water.
class VegetationPatchFeature final : public Feature {
public:
    TagId replaceable = 0;
    BlockStateProvider ground = BlockStateProvider::simple(0);
    const PlacedFeature* vegetation = nullptr;
    bool ceiling = false;
    IntProvider depth, xzRadius;
    float extraBottomBlockChance = 0.0f, vegetationChance = 0.0f, extraEdgeColumnChance = 0.0f;
    int verticalRange = 5;
    bool waterlogged = false;
    StateId water = 0;

    bool place(FeatureContext& ctx) const override {
        WorldGenRegion& level = ctx.level;
        Xoroshiro& r = ctx.random;
        const int rx = xzRadius.sample(r) + 1, rz = xzRadius.sample(r) + 1;
        // Vanilla keeps a HashSet; columns differ in x/z, so a list in column order
        // holds the same positions without hashing.
        std::vector<BlockPos> surface;
        surface.reserve(size_t(std::max(0, 2 * rx + 1) * std::max(0, 2 * rz + 1)));
        placeGroundPatch(level, r, ctx.origin, rx, rz, surface);
        if (waterlogged) {
            // Pool: surface blocks walled in on four sides and below become water.
            size_t kept = 0;
            for (const BlockPos& s : surface)
                if (!exposed(level, s)) surface[kept++] = s;
            surface.resize(kept);
            for (const BlockPos& s : surface) level.setBlock(s, water);
        }
        PlacementContext nested{level};
        const Direction away = ceiling ? Direction::Down : Direction::Up; // from the surface into the cave
        for (const BlockPos& s : surface) {
            if (!(vegetationChance > 0.0f) || !(r.nextFloat() < vegetationChance)) continue;
            if (!waterlogged) {
                vegetation->place(nested, r, s.offset(away));
                continue;
            }
            // In the pool block itself (vanilla offsets from the block below it).
            if (!vegetation->place(nested, r, s.below().offset(away))) continue;
            const StateId v = level.getBlock(s);
            if (level.registry().value(v, "waterlogged") == "false")
                level.setBlock(s, withProp(level.registry(), v, "waterlogged", true));
        }
        return !surface.empty();
    }

private:
    void placeGroundPatch(WorldGenRegion& level, Xoroshiro& r, const BlockPos& o, int rx, int rz,
                          std::vector<BlockPos>& out) const {
        const Direction toSurface = ceiling ? Direction::Up : Direction::Down;
        const Direction back = opposite(toSurface);
        for (int dx = -rx; dx <= rx; ++dx) {
            const bool edgeX = dx == -rx || dx == rx;
            for (int dz = -rz; dz <= rz; ++dz) {
                const bool edgeZ = dz == -rz || dz == rz;
                const bool corner = edgeX && edgeZ, edge = (edgeX || edgeZ) && !corner;
                if (corner || (edge && (extraEdgeColumnChance == 0.0f || r.nextFloat() > extraEdgeColumnChance)))
                    continue;
                // Walk to the last free block before the surface.
                BlockPos p = o.offset(dx, 0, dz);
                for (int m = 0; level.isAir(p) && m < verticalRange; ++m) p = p.offset(toSurface);
                for (int m = 0; !level.isAir(p) && m < verticalRange; ++m) p = p.offset(back);
                const BlockPos s = p.offset(toSurface);
                if (!level.isAir(p) || !level.sturdy(s, back)) continue;
                const int n = depth.sample(r) +
                              (extraBottomBlockChance > 0.0f && r.nextFloat() < extraBottomBlockChance ? 1 : 0);
                if (placeGround(level, r, s, n, toSurface)) out.push_back(s);
            }
        }
    }

    bool placeGround(WorldGenRegion& level, Xoroshiro& r, BlockPos p, int n, Direction dir) const {
        const BlockRegistry& reg = level.registry();
        for (int i = 0; i < n; ++i) {
            const StateId g = ground.get(r, p);
            const StateId cur = level.getBlock(p);
            // Vanilla does not step past ground that is already there.
            if (reg.info(g).block == reg.info(cur).block) continue;
            if (!reg.hasTag(cur, replaceable)) return i != 0;
            level.setBlock(p, g);
            p = p.offset(dir);
        }
        return true;
    }

    static bool exposed(const WorldGenRegion& level, const BlockPos& s) {
        for (Direction d : {Direction::North, Direction::East, Direction::South, Direction::West, Direction::Down})
            if (!level.sturdy(s.offset(d), opposite(d))) return true;
        return false;
    }
};

std::unique_ptr<Feature> makeVegetationPatch(const Json& c, FeatureLoader& l, bool waterlogged) {
    auto replaceable = l.tag(c.value("replaceable", Json()));
    auto ground = BlockStateProvider::parse(c.value("ground_state", Json()), l);
    const PlacedFeature* vegetation = l.placed(c.value("vegetation_feature", Json()));
    auto depth = IntProvider::parse(c.value("depth", Json(1)), l);
    auto radius = IntProvider::parse(c.value("xz_radius", Json()), l);
    const std::string surface = jsonGet<std::string>(c, "surface", "floor");
    if (surface != "floor" && surface != "ceiling") l.error("surface should be floor or ceiling");
    if (!replaceable || !ground || !vegetation || !depth || !radius || (surface != "floor" && surface != "ceiling"))
        return nullptr;
    auto f = std::make_unique<VegetationPatchFeature>();
    f->replaceable = *replaceable;
    f->ground = *ground;
    f->vegetation = vegetation;
    f->ceiling = surface == "ceiling";
    f->depth = *depth;
    f->xzRadius = *radius;
    f->extraBottomBlockChance = l.getFloat(c, "extra_bottom_block_chance", 0.0f);
    f->verticalRange = l.getInt(c, "vertical_range", 5);
    f->vegetationChance = l.getFloat(c, "vegetation_chance", 0.0f);
    f->extraEdgeColumnChance = l.getFloat(c, "extra_edge_column_chance", 0.0f);
    f->waterlogged = waterlogged;
    f->water = l.registry.defaultState("water");
    return f;
}

// ---------------------------------------------------------------- random_boolean_selector

class RandomBooleanSelectorFeature final : public Feature {
public:
    const PlacedFeature *whenTrue = nullptr, *whenFalse = nullptr;

    bool place(FeatureContext& ctx) const override {
        PlacementContext nested{ctx.level};
        return (ctx.random.nextBool() ? whenTrue : whenFalse)->place(nested, ctx.random, ctx.origin);
    }
};

std::unique_ptr<Feature> makeRandomBooleanSelector(const Json& c, FeatureLoader& l) {
    auto f = std::make_unique<RandomBooleanSelectorFeature>();
    f->whenTrue = l.placed(c.value("feature_true", Json()));
    f->whenFalse = l.placed(c.value("feature_false", Json()));
    if (!f->whenTrue || !f->whenFalse) return nullptr;
    return f;
}

} // namespace

void registerCaveFeatures(FeatureSet& set) {
    set.registerFeature("lake", makeLake);
    set.registerFeature("spring_feature", makeSpring);
    set.registerFeature("underwater_magma", makeUnderwaterMagma);
    set.registerFeature("multiface_growth", makeMultifaceGrowth);
    set.registerFeature("vegetation_patch",
                        [](const Json& c, FeatureLoader& l) { return makeVegetationPatch(c, l, false); });
    set.registerFeature("waterlogged_vegetation_patch",
                        [](const Json& c, FeatureLoader& l) { return makeVegetationPatch(c, l, true); });
    set.registerFeature("random_boolean_selector", makeRandomBooleanSelector);
    registerDripstoneFeatures(set);
}

} // namespace mc
