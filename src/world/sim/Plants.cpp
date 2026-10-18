#include "world/sim/Plants.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "entity/ItemEntity.h"
#include "game/Item.h"
#include "resources/ResourcePack.h"
#include "world/block/BlockRules.h"
#include "world/gen/feature/BiomeDecorator.h"
#include "world/level/Level.h"
#include "world/sim/LiveRegion.h"

namespace mc::sim {
namespace {

constexpr uint16_t kNone = 0xFFFF;

// Block indices and tags the plants look for, cached per registry.
struct Ids {
    uint16_t farmland, pumpkin, melon, sugarCane, cactus, cactusFlower, kelp, kelpPlant, caveVines, caveVinesPlant, bamboo,
        vine, water, dirt, snow, seagrass, tallSeagrass, hangingRoots;
    TagId dirtTag, logs, flowers, snowTag, mushroomGrow, corals, wallCorals;
    explicit Ids(const BlockRegistry& reg) {
        auto idx = [&](std::string_view id) {
            const Block* b = reg.find(id);
            return b ? b->index : kNone;
        };
        auto tag = [&](std::string_view name) { return reg.tag(name).value_or(TagId(0)); };
        farmland = idx("farmland");
        pumpkin = idx("pumpkin");
        melon = idx("melon");
        sugarCane = idx("sugar_cane");
        cactus = idx("cactus");
        cactusFlower = idx("cactus_flower");
        kelp = idx("kelp");
        kelpPlant = idx("kelp_plant");
        caveVines = idx("cave_vines");
        caveVinesPlant = idx("cave_vines_plant");
        bamboo = idx("bamboo");
        vine = idx("vine");
        water = idx("water");
        dirt = idx("dirt");
        snow = idx("snow");
        seagrass = idx("seagrass");
        tallSeagrass = idx("tall_seagrass");
        hangingRoots = idx("hanging_roots");
        dirtTag = tag("dirt");
        logs = tag("logs");
        flowers = tag("flowers");
        snowTag = tag("snow");
        mushroomGrow = tag("mushroom_grow_block");
        corals = tag("corals");
        wallCorals = tag("wall_corals");
    }
};

const Ids& ids(const BlockRegistry& reg) {
    static thread_local const BlockRegistry* owner = nullptr;
    static thread_local std::optional<Ids> cached;
    if (owner != &reg) {
        cached.emplace(reg);
        owner = &reg;
    }
    return *cached;
}

bool is(const BlockRegistry& reg, StateId s, uint16_t block) { return reg.info(s).block == block; }
bool isAir(const Level& level, const BlockPos& p) { return level.registry().info(level.getBlock(p)).flags & kAir; }

StateId withValue(const BlockRegistry& reg, StateId s, std::string_view prop, std::string_view value) {
    return reg.with(s, prop, value).value_or(s);
}
StateId withInt(const BlockRegistry& reg, StateId s, std::string_view prop, int value) {
    return withValue(reg, s, prop, std::to_string(value));
}
StateId withBool(const BlockRegistry& reg, StateId s, std::string_view prop, bool value) {
    return withValue(reg, s, prop, value ? "true" : "false");
}

int maxAge(const BlockRegistry& reg, StateId s) {
    const Block& b = reg.blockOf(s);
    for (uint16_t p : b.props)
        if (reg.property(p).name == "age") return int(reg.property(p).values.size()) - 1;
    return 0;
}

Direction counterClockwise(Direction d) { return opposite(rotateYClockwise(d)); }

// Vanilla Mth.nextInt(random, lo, hi).
int nextIntBetween(Xoroshiro& r, int lo, int hi) { return lo >= hi ? lo : r.nextInt(hi - lo + 1) + lo; }

// Pops an item out of a block (vanilla Block.popResource).
void popItem(Level& level, const BlockPos& pos, const Block& block) {
    if (!level.itemRegistry()) return;
    if (const Item* item = level.itemRegistry()->forBlock(block)) ItemEntity::popResource(level, pos, ItemStack{item, 1});
}

// ---------------------------------------------------------------- crops and stems

// Vanilla CropBlock.getGrowthSpeed: farmland under and around (wet counts triple), halved
// when the same crop stands in rows on both axes or diagonally.
float growthSpeed(const Level& level, const BlockPos& pos, uint16_t self) {
    const BlockRegistry& reg = level.registry();
    const Ids& id = ids(reg);
    float f = 1.0f;
    const BlockPos below = pos.below();
    for (int i = -1; i <= 1; ++i)
        for (int j = -1; j <= 1; ++j) {
            float g = 0.0f;
            const StateId s = level.getBlock(below.offset(i, 0, j));
            if (is(reg, s, id.farmland)) g = reg.intValue(s, "moisture") > 0 ? 3.0f : 1.0f;
            if (i != 0 || j != 0) g /= 4.0f;
            f += g;
        }
    auto same = [&](const BlockPos& p) { return is(reg, level.getBlock(p), self); };
    const BlockPos n = pos.offset(Direction::North), s = pos.offset(Direction::South);
    const BlockPos w = pos.offset(Direction::West), e = pos.offset(Direction::East);
    const bool rowX = same(w) || same(e), rowZ = same(n) || same(s);
    if (rowX && rowZ) f /= 2.0f;
    else if (same(w.offset(Direction::North)) || same(e.offset(Direction::North)) || same(e.offset(Direction::South)) ||
             same(w.offset(Direction::South)))
        f /= 2.0f;
    return f;
}

// The crop at `age` (vanilla getStateForAge): a torchflower crop past age 1 is a torchflower.
StateId cropForAge(const BlockRegistry& reg, StateId crop, int age) {
    if (shortId(reg.blockOf(crop).id) == "torchflower_crop" && age >= 2) return reg.defaultState("torchflower");
    return withInt(reg, crop, "age", age);
}
int cropMaxAge(const BlockRegistry& reg, StateId crop) {
    return shortId(reg.blockOf(crop).id) == "torchflower_crop" ? 2 : maxAge(reg, crop);
}

void cropTick(Level& level, const BlockPos& pos, StateId state) {
    const BlockRegistry& reg = level.registry();
    Xoroshiro& r = level.random();
    const std::string_view id = shortId(reg.blockOf(state).id);
    const int age = reg.intValue(state, "age");
    if (id == "nether_wart") { // NetherWartBlock: 1 in 10, in any light
        if (age < 3 && r.nextInt(10) == 0) level.setBlock(pos, withInt(reg, state, "age", age + 1));
        return;
    }
    if ((id == "beetroots" || id == "torchflower_crop") && r.nextInt(3) == 0) return; // they grow slower
    if (level.rawBrightness(pos, 0) < 9 || age >= cropMaxAge(reg, state)) return;
    const float f = growthSpeed(level, pos, reg.info(state).block);
    if (r.nextInt(int(25.0f / f) + 1) == 0) level.setBlock(pos, cropForAge(reg, state, age + 1));
}

void stemTick(Level& level, const BlockPos& pos, StateId state) {
    const BlockRegistry& reg = level.registry();
    const Ids& id = ids(reg);
    Xoroshiro& r = level.random();
    if (level.rawBrightness(pos, 0) < 9) return;
    const float f = growthSpeed(level, pos, reg.info(state).block);
    if (r.nextInt(int(25.0f / f) + 1) != 0) return;
    const int age = reg.intValue(state, "age");
    if (age < 7) {
        level.setBlock(pos, withInt(reg, state, "age", age + 1));
        return;
    }
    // Ripe: a fruit on a random side, onto farmland or dirt, and the stem bends to it.
    const Direction d = kHorizontal[size_t(r.nextInt(4))];
    const BlockPos at = pos.offset(d);
    const StateId under = level.getBlock(at.below());
    if (!isAir(level, at) || !(is(reg, under, id.farmland) || reg.hasTag(under, id.dirtTag))) return;
    const bool pumpkin = shortId(reg.blockOf(state).id) == "pumpkin_stem";
    level.setBlockAndUpdate(at, reg.defaultState(pumpkin ? "pumpkin" : "melon"));
    const StateId attached = reg.defaultState(pumpkin ? "attached_pumpkin_stem" : "attached_melon_stem");
    level.setBlockAndUpdate(pos, withValue(reg, attached, "facing", directionName(d)));
}

// ---------------------------------------------------------------- saplings and trees

// Vanilla TreeGrower: which feature a sapling grows (names in data/worldgen/features).
struct TreeGrower {
    std::string_view sapling;
    float secondaryChance;
    const char *mega, *secondaryMega, *tree, *secondaryTree, *flowers, *secondaryFlowers;
};
constexpr std::array kTreeGrowers = {
    TreeGrower{"oak_sapling", 0.1f, nullptr, nullptr, "oak_checked", "fancy_oak_checked", "oak_bees_005",
               "fancy_oak_bees_005"},
    TreeGrower{"spruce_sapling", 0.5f, "mega_spruce_checked", "mega_pine_checked", "spruce_checked", nullptr, nullptr,
               nullptr},
    TreeGrower{"birch_sapling", 0.0f, nullptr, nullptr, "birch", nullptr, "birch_bees_005", nullptr},
    TreeGrower{"jungle_sapling", 0.0f, "mega_jungle_tree", nullptr, "jungle_tree_no_vine", nullptr, nullptr, nullptr},
    TreeGrower{"acacia_sapling", 0.0f, nullptr, nullptr, "acacia_checked", nullptr, nullptr, nullptr},
    TreeGrower{"cherry_sapling", 0.0f, nullptr, nullptr, "cherry", nullptr, "cherry_bees_005", nullptr},
    TreeGrower{"dark_oak_sapling", 0.0f, "dark_oak", nullptr, nullptr, nullptr, nullptr, nullptr},
    TreeGrower{"azalea", 0.0f, nullptr, nullptr, "azalea_tree", nullptr, nullptr, nullptr},
    TreeGrower{"flowering_azalea", 0.0f, nullptr, nullptr, "azalea_tree", nullptr, nullptr, nullptr},
};

// Vanilla hasFlowers: a flower within 2 blocks sideways and 1 up or down.
bool hasFlowers(const Level& level, const BlockPos& pos) {
    const BlockRegistry& reg = level.registry();
    for (int y = -1; y <= 1; ++y)
        for (int z = -2; z <= 2; ++z)
            for (int x = -2; x <= 2; ++x)
                if (reg.hasTag(level.getBlock(pos.offset(x, y, z)), ids(reg).flowers)) return true;
    return false;
}

void advanceTree(Level& level, const BlockPos& pos, StateId state) {
    const BlockRegistry& reg = level.registry();
    if (reg.intValue(state, "stage") == 0) level.setBlock(pos, withInt(reg, state, "stage", 1));
    else growTree(level, pos, state);
}

// ---------------------------------------------------------------- tall plants

void sugarCaneTick(Level& level, const BlockPos& pos, StateId state) {
    const BlockRegistry& reg = level.registry();
    if (!isAir(level, pos.above())) return;
    int height = 1;
    while (is(reg, level.getBlock(pos.below(height)), ids(reg).sugarCane)) ++height;
    if (height >= 3) return;
    const int age = reg.intValue(state, "age");
    if (age == 15) {
        level.setBlockAndUpdate(pos.above(), reg.blockOf(state).defaultState);
        level.setBlock(pos, withInt(reg, state, "age", 0));
    } else {
        level.setBlock(pos, withInt(reg, state, "age", age + 1));
    }
}

void cactusTick(Level& level, const BlockPos& pos, StateId state) {
    // Vanilla 1.21.5 CactusBlock: grows to 3 tall; at age 8 it may flower instead.
    const BlockRegistry& reg = level.registry();
    const Ids& id = ids(reg);
    const BlockPos up = pos.above();
    if (!isAir(level, up)) return;
    const int age = reg.intValue(state, "age");
    int height = 1;
    while (is(reg, level.getBlock(pos.below(height)), id.cactus))
        if (++height == 3 && age == 15) return;
    const StateId cactus = reg.blockOf(state).defaultState;
    if (age == 8 && id.cactusFlower != kNone && reg.rules().canSurvive(level, cactus, up)) {
        if (level.random().nextDouble() <= (height >= 3 ? 0.25 : 0.1))
            level.setBlockAndUpdate(up, reg.block(id.cactusFlower).defaultState);
    } else if (age == 15 && height < 3) {
        level.setBlockAndUpdate(up, cactus);
        level.setBlock(pos, withInt(reg, state, "age", 0));
    }
    if (age < 15) level.setBlock(pos, withInt(reg, state, "age", age + 1));
}

// Kelp grows up into water sources, cave vines down into air (vanilla GrowingPlantHeadBlock).
struct GrowingPlant {
    Direction growth;
    double chance; // per random tick
    uint16_t head, body;
};
GrowingPlant growingPlant(const BlockRegistry& reg, StateId s) {
    const Ids& id = ids(reg);
    const uint16_t b = reg.info(s).block;
    if (b == id.kelp || b == id.kelpPlant) return {Direction::Up, 0.14, id.kelp, id.kelpPlant};
    return {Direction::Down, 0.1, id.caveVines, id.caveVinesPlant};
}
bool canGrowInto(const Level& level, const GrowingPlant& g, StateId s) {
    const BlockRegistry& reg = level.registry();
    if (g.head == ids(reg).kelp) return is(reg, s, ids(reg).water) && reg.info(s).fluid.source();
    return reg.info(s).flags & kAir;
}
StateId grownHead(Level& level, const GrowingPlant& g, StateId head, int age) {
    const BlockRegistry& reg = level.registry();
    StateId s = withInt(reg, head, "age", std::min(age, 25));
    if (g.head == ids(reg).caveVines) s = withBool(reg, s, "berries", level.random().nextFloat() < 0.11f);
    return s;
}

void growingPlantTick(Level& level, const BlockPos& pos, StateId state) {
    const GrowingPlant g = growingPlant(level.registry(), state);
    const int age = level.registry().intValue(state, "age");
    if (age >= 25 || level.random().nextDouble() >= g.chance) return;
    const BlockPos to = pos.offset(g.growth);
    if (canGrowInto(level, g, level.getBlock(to))) level.setBlockAndUpdate(to, grownHead(level, g, state, age + 1));
}

// ---------------------------------------------------------------- bamboo

int bambooBelow(const Level& level, const BlockPos& pos) {
    int n = 0;
    while (n < 16 && is(level.registry(), level.getBlock(pos.below(n + 1)), ids(level.registry()).bamboo)) ++n;
    return n;
}
int bambooAbove(const Level& level, const BlockPos& pos) {
    int n = 0;
    while (n < 16 && is(level.registry(), level.getBlock(pos.above(n + 1)), ids(level.registry()).bamboo)) ++n;
    return n;
}

// Vanilla BambooStalkBlock.growBamboo: a new top over `pos`; the leaves move up with it.
void growBamboo(Level& level, const BlockPos& pos, StateId state, int height) {
    const BlockRegistry& reg = level.registry();
    const uint16_t bamboo = ids(reg).bamboo;
    const StateId below = level.getBlock(pos.below());
    const BlockPos below2Pos = pos.below(2);
    const StateId below2 = level.getBlock(below2Pos);
    std::string_view leaves = "none";
    if (height >= 1) {
        if (!is(reg, below, bamboo) || reg.value(below, "leaves") == "none") {
            leaves = "small";
        } else {
            leaves = "large";
            if (is(reg, below2, bamboo)) {
                level.setBlockAndUpdate(pos.below(), withValue(reg, below, "leaves", "small"));
                level.setBlockAndUpdate(below2Pos, withValue(reg, below2, "leaves", "none"));
            }
        }
    }
    const int age = reg.intValue(state, "age") != 1 && !is(reg, below2, bamboo) ? 0 : 1;
    const int stage = (height < 11 || !(level.random().nextFloat() < 0.25f)) && height != 15 ? 0 : 1;
    StateId top = withInt(reg, reg.block(bamboo).defaultState, "age", age);
    top = withValue(reg, top, "leaves", leaves);
    top = withInt(reg, top, "stage", stage);
    level.setBlockAndUpdate(pos.above(), top);
}

void bambooTick(Level& level, const BlockPos& pos, StateId state) {
    if (level.registry().intValue(state, "stage") != 0) return;
    if (level.random().nextInt(3) == 0 && isAir(level, pos.above()) && level.rawBrightness(pos.above(), 0) >= 9) {
        const int height = bambooBelow(level, pos) + 1;
        if (height < 16) growBamboo(level, pos, state, height);
    }
}

void bambooSaplingGrow(Level& level, const BlockPos& pos) {
    const BlockRegistry& reg = level.registry();
    level.setBlockAndUpdate(pos.above(), withValue(reg, reg.block(ids(reg).bamboo).defaultState, "leaves", "small"));
}

// ---------------------------------------------------------------- vines

std::string_view faceProperty(Direction d) { return directionName(d); }

bool acceptableNeighbour(const Level& level, const BlockPos& p, Direction d) { return level.sturdy(p, opposite(d)); }

bool hasHorizontalConnection(const BlockRegistry& reg, StateId s) {
    for (Direction d : kHorizontal)
        if (reg.boolValue(s, faceProperty(d))) return true;
    return false;
}

// Vanilla VineBlock.canSpread: at most 4 vines in the 9x3x9 around.
bool vineCanSpread(const Level& level, const BlockPos& pos) {
    const BlockRegistry& reg = level.registry();
    int left = 5;
    for (int x = -4; x <= 4; ++x)
        for (int y = -1; y <= 1; ++y)
            for (int z = -4; z <= 4; ++z)
                if (is(reg, level.getBlock(pos.offset(x, y, z)), ids(reg).vine) && --left <= 0) return false;
    return true;
}

bool vineCanSupportAtFace(const Level& level, const BlockPos& pos, Direction d) {
    if (d == Direction::Down) return false;
    if (acceptableNeighbour(level, pos.offset(d), d)) return true;
    if (axisOf(d) == Axis::Y) return false;
    const StateId above = level.getBlock(pos.above());
    return is(level.registry(), above, ids(level.registry()).vine) && level.registry().boolValue(above, faceProperty(d));
}

void vineTick(Level& level, const BlockPos& pos, StateId state) {
    const BlockRegistry& reg = level.registry();
    Xoroshiro& r = level.random();
    if (!level.doVinesSpread || r.nextInt(4) != 0) return;
    const StateId vine = reg.blockOf(state).defaultState;
    const Direction d = kDirections[size_t(r.nextInt(6))];
    const BlockPos up = pos.above();
    if (axisOf(d) != Axis::Y && !reg.boolValue(state, faceProperty(d))) {
        if (!vineCanSpread(level, pos)) return;
        const BlockPos to = pos.offset(d);
        if (isAir(level, to)) {
            const Direction cw = rotateYClockwise(d), ccw = counterClockwise(d);
            const bool onCw = reg.boolValue(state, faceProperty(cw)), onCcw = reg.boolValue(state, faceProperty(ccw));
            const BlockPos toCw = to.offset(cw), toCcw = to.offset(ccw);
            if (onCw && acceptableNeighbour(level, toCw, cw)) {
                level.setBlock(to, withBool(reg, vine, faceProperty(cw), true));
            } else if (onCcw && acceptableNeighbour(level, toCcw, ccw)) {
                level.setBlock(to, withBool(reg, vine, faceProperty(ccw), true));
            } else {
                const Direction back = opposite(d);
                if (onCw && isAir(level, toCw) && acceptableNeighbour(level, pos.offset(cw), back)) {
                    level.setBlock(toCw, withBool(reg, vine, faceProperty(back), true));
                } else if (onCcw && isAir(level, toCcw) && acceptableNeighbour(level, pos.offset(ccw), back)) {
                    level.setBlock(toCcw, withBool(reg, vine, faceProperty(back), true));
                } else if (r.nextFloat() < 0.05 && acceptableNeighbour(level, to.above(), Direction::Up)) {
                    level.setBlock(to, withBool(reg, vine, "up", true));
                }
            }
        } else if (acceptableNeighbour(level, to, d)) {
            level.setBlock(pos, withBool(reg, state, faceProperty(d), true));
        }
        return;
    }
    if (d == Direction::Up && pos.y < kWorldMaxY) {
        if (vineCanSupportAtFace(level, pos, d)) {
            level.setBlock(pos, withBool(reg, state, "up", true));
            return;
        }
        if (isAir(level, up)) {
            if (!vineCanSpread(level, pos)) return;
            StateId grown = state;
            for (Direction h : kHorizontal)
                if (r.nextBool() || !acceptableNeighbour(level, up.offset(h), h)) grown = withBool(reg, grown, faceProperty(h), false);
            if (hasHorizontalConnection(reg, grown)) level.setBlock(up, grown);
            return;
        }
    }
    if (pos.y > kWorldMinY) {
        // Hanging down: a random part of this vine's sides.
        const BlockPos down = pos.below();
        const StateId below = level.getBlock(down);
        const bool belowAir = reg.info(below).flags & kAir;
        if (belowAir || is(reg, below, ids(reg).vine)) {
            const StateId from = belowAir ? vine : below;
            StateId to = from;
            for (Direction h : kHorizontal)
                if (r.nextBool() && reg.boolValue(state, faceProperty(h))) to = withBool(reg, to, faceProperty(h), true);
            if (to != from && hasHorizontalConnection(reg, to)) level.setBlock(down, to);
        }
    }
}

// ---------------------------------------------------------------- small plants

void berryBushTick(Level& level, const BlockPos& pos, StateId state) {
    const BlockRegistry& reg = level.registry();
    const int age = reg.intValue(state, "age");
    if (age < 3 && level.random().nextInt(5) == 0 && level.rawBrightness(pos.above(), 0) >= 9)
        level.setBlock(pos, withInt(reg, state, "age", age + 1));
}

void cocoaTick(Level& level, const BlockPos& pos, StateId state) {
    const BlockRegistry& reg = level.registry();
    if (level.random().nextInt(5) != 0) return;
    const int age = reg.intValue(state, "age");
    if (age < 2) level.setBlock(pos, withInt(reg, state, "age", age + 1));
}

// Vanilla MushroomBlock.canSurvive: on mycelium-like blocks anywhere, else in the dark.
bool mushroomSurvives(const Level& level, StateId state, const BlockPos& pos) {
    const BlockRegistry& reg = level.registry();
    if (reg.hasTag(level.getBlock(pos.below()), ids(reg).mushroomGrow)) return true;
    return level.rawBrightness(pos, 0) < 13 && reg.rules().canSurvive(level, state, pos);
}

void mushroomTick(Level& level, const BlockPos& pos, StateId state) {
    const BlockRegistry& reg = level.registry();
    Xoroshiro& r = level.random();
    if (r.nextInt(25) != 0) return;
    int left = 5;
    for (int x = -4; x <= 4; ++x)
        for (int y = -1; y <= 1; ++y)
            for (int z = -4; z <= 4; ++z)
                if (is(reg, level.getBlock(pos.offset(x, y, z)), reg.info(state).block) && --left <= 0) return;
    auto step = [&](const BlockPos& from) {
        const int dx = r.nextInt(3) - 1;
        const int a = r.nextInt(2);
        const int dy = a - r.nextInt(2);
        const int dz = r.nextInt(3) - 1;
        return from.offset(dx, dy, dz);
    };
    BlockPos from = pos;
    BlockPos to = step(from);
    for (int k = 0; k < 4; ++k) {
        if (isAir(level, to) && mushroomSurvives(level, state, to)) from = to;
        to = step(from);
    }
    if (isAir(level, to) && mushroomSurvives(level, state, to)) level.setBlock(to, state);
}

// ---------------------------------------------------------------- grass

// Vanilla SpreadingSnowyDirtBlock.canBeGrass: light reaches it (a single snow layer is fine;
// a full fluid or a light-blocking block above is not).
bool canBeGrass(const Level& level, const BlockPos& pos) {
    const BlockRegistry& reg = level.registry();
    const StateId above = level.getBlock(pos.above());
    if (is(reg, above, ids(reg).snow) && reg.intValue(above, "layers") == 1) return true;
    const StateInfo& info = reg.info(above);
    if (info.fluid.kind != FluidKind::None && (info.fluid.source() || info.fluid.falling)) return false;
    if (info.lightBlockingFaces & (1u << int(Direction::Down))) return false;
    return info.opacity < 15;
}

void grassTick(Level& level, const BlockPos& pos, StateId state) {
    const BlockRegistry& reg = level.registry();
    const Ids& id = ids(reg);
    if (!canBeGrass(level, pos)) {
        level.setBlockAndUpdate(pos, reg.block(id.dirt).defaultState);
        return;
    }
    if (level.rawBrightness(pos.above(), level.skyDarken()) < 9) return;
    const StateId spread = reg.blockOf(state).defaultState;
    Xoroshiro& r = level.random();
    for (int i = 0; i < 4; ++i) {
        const int dx = r.nextInt(3) - 1;
        const int dy = r.nextInt(5) - 3;
        const int dz = r.nextInt(3) - 1;
        const BlockPos p = pos.offset(dx, dy, dz);
        if (!is(reg, level.getBlock(p), id.dirt) || !canBeGrass(level, p) || level.fluid(p.above()).kind == FluidKind::Water)
            continue;
        level.setBlockAndUpdate(p, withBool(reg, spread, "snowy", reg.hasTag(level.getBlock(p.above()), id.snowTag)));
    }
}

// ---------------------------------------------------------------- leaves

int leafDistanceAt(const BlockRegistry& reg, StateId s) {
    if (reg.hasTag(s, ids(reg).logs)) return 0;
    const int d = reg.intValue(s, "distance", -1);
    return d >= 0 && reg.blockOf(s).className == "leaves" ? d : 7;
}

StateId withUpdatedDistance(const Level& level, const BlockPos& pos, StateId state) {
    const BlockRegistry& reg = level.registry();
    int distance = 7;
    for (Direction d : kDirections) {
        distance = std::min(distance, leafDistanceAt(reg, level.getBlock(pos.offset(d))) + 1);
        if (distance == 1) break;
    }
    return withInt(reg, state, "distance", distance);
}

void leavesRandomTick(Level& level, const BlockPos& pos, StateId state) {
    // Decaying: no log within 6 and not placed by a player.
    const BlockRegistry& reg = level.registry();
    if (reg.boolValue(state, "persistent") || reg.intValue(state, "distance") != 7) return;
    level.dropResources(pos, state);
    level.setBlockAndUpdate(pos, reg.boolValue(state, "waterlogged") ? reg.defaultState("water") : BlockRegistry::kAirState);
}

// ---------------------------------------------------------------- bone meal

// Vanilla BonemealableBlock.findSpreadableNeighbourPos: a random free side `state` survives on.
std::optional<BlockPos> spreadableNeighbour(Level& level, const BlockPos& pos, StateId state) {
    std::array<Direction, 4> sides = kHorizontal;
    for (size_t i = sides.size(); i > 1; --i) std::swap(sides[i - 1], sides[size_t(level.random().nextInt(int(i)))]);
    for (Direction d : sides) {
        const BlockPos p = pos.offset(d);
        if (isAir(level, p) && level.registry().rules().canSurvive(level, state, p)) return p;
    }
    return std::nullopt;
}

// The kelp or cave vine head at the end of the body at `pos` (vanilla getHeadPos).
std::optional<BlockPos> growingPlantHead(const Level& level, const BlockPos& pos, const GrowingPlant& g) {
    BlockPos p = pos;
    StateId s;
    do {
        p = p.offset(g.growth);
        s = level.getBlock(p);
    } while (is(level.registry(), s, g.body));
    return is(level.registry(), s, g.head) ? std::optional(p) : std::nullopt;
}

// Vanilla GrassBlock.performBonemeal: short grass (sometimes the biome's flowers) on random
// walks across the grass around.
void boneMealGrass(Level& level, const BlockPos& pos) {
    const BlockRegistry& reg = level.registry();
    Xoroshiro& r = level.random();
    const BlockPos start = pos.above();
    const StateId shortGrass = reg.defaultState("short_grass");
    for (int i = 0; i < 128; ++i) {
        BlockPos p = start;
        bool blocked = false;
        for (int j = 0; j < i / 16; ++j) {
            const int dx = r.nextInt(3) - 1;
            const int a = r.nextInt(3) - 1;
            const int dy = a * r.nextInt(3) / 2;
            const int dz = r.nextInt(3) - 1;
            p = p.offset(dx, dy, dz);
            if (!is(reg, level.getBlock(p.below()), reg.info(level.getBlock(pos)).block) ||
                reg.info(level.getBlock(p)).collision == 1) {
                blocked = true;
                break;
            }
        }
        if (blocked) continue;
        const StateId there = level.getBlock(p);
        if (is(reg, there, reg.info(shortGrass).block) && r.nextInt(10) == 0) {
            // Short grass grows into tall grass.
            const BlockPos above = p.above();
            if (isAir(level, above)) {
                const StateId tall = reg.defaultState("tall_grass");
                level.setBlockAndUpdate(p, withValue(reg, tall, "half", "lower"));
                level.setBlockAndUpdate(above, withValue(reg, tall, "half", "upper"));
            }
        }
        if (!(reg.info(there).flags & kAir)) continue;
        if (r.nextInt(8) == 0) {
            if (!level.decorator()) continue;
            const auto& flowers = level.decorator()->flowerFeatures(level.biomeAt(p));
            if (flowers.empty()) continue;
            const PlacedFeature* flower = flowers[size_t(r.nextInt(int(flowers.size())))];
            LiveRegion region(level, *level.biomes());
            PlacementContext ctx{region};
            flower->place(ctx, r, p);
        } else if (reg.rules().canSurvive(level, shortGrass, p)) {
            level.setBlockAndUpdate(p, shortGrass); // vanilla GRASS_BONEMEAL: short grass where it can stand
        }
    }
}

// Vanilla BoneMealItem.growWaterPlant: seagrass (and corals in warm oceans) on random walks
// through the water.
bool boneMealWater(Level& level, const BlockPos& pos, Direction face) {
    const BlockRegistry& reg = level.registry();
    const Ids& id = ids(reg);
    const StateId at = level.getBlock(pos);
    if (!is(reg, at, id.water) || !reg.info(at).fluid.source()) return false;
    Xoroshiro& r = level.random();
    const auto warmOcean = level.biomes() ? level.biomes()->find("warm_ocean") : std::nullopt;
    std::vector<uint16_t> underwater; // vanilla #underwater_bonemeals
    underwater.push_back(id.seagrass);
    for (uint16_t b : reg.tags().blocks(id.corals)) underwater.push_back(b);
    for (uint16_t b : reg.tags().blocks(id.wallCorals)) underwater.push_back(b);
    const std::vector<uint16_t> wallCorals = reg.tags().blocks(id.wallCorals);
    for (int i = 0; i < 128; ++i) {
        BlockPos p = pos;
        StateId plant = reg.block(id.seagrass).defaultState;
        bool blocked = false;
        for (int j = 0; j < i / 16; ++j) {
            const int dx = r.nextInt(3) - 1;
            const int a = r.nextInt(3) - 1;
            const int dy = a * r.nextInt(3) / 2;
            const int dz = r.nextInt(3) - 1;
            p = p.offset(dx, dy, dz);
            if (reg.info(level.getBlock(p)).collision == 1) {
                blocked = true;
                break;
            }
        }
        if (blocked) continue;
        if (warmOcean && level.biomeAt(p) == *warmOcean) {
            if (i == 0 && axisOf(face) != Axis::Y && !wallCorals.empty()) {
                plant = reg.block(wallCorals[size_t(r.nextInt(int(wallCorals.size())))]).defaultState;
                plant = withValue(reg, plant, "facing", directionName(face));
            } else if (r.nextInt(4) == 0) {
                plant = reg.block(underwater[size_t(r.nextInt(int(underwater.size())))]).defaultState;
            }
        }
        if (reg.hasTag(plant, id.wallCorals))
            for (int k = 0; !reg.rules().canSurvive(level, plant, p) && k < 4; ++k)
                plant = withValue(reg, plant, "facing", directionName(kHorizontal[size_t(r.nextInt(4))]));
        if (!reg.rules().canSurvive(level, plant, p)) continue;
        const StateId there = level.getBlock(p);
        if (is(reg, there, id.water) && reg.info(there).fluid.source()) {
            level.setBlockAndUpdate(p, plant);
        } else if (is(reg, there, id.seagrass) && r.nextInt(10) == 0 && is(reg, level.getBlock(p.above()), id.water)) {
            const StateId tall = reg.block(id.tallSeagrass).defaultState;
            level.setBlock(p, withValue(reg, tall, "half", "lower"));
            level.setBlock(p.above(), withValue(reg, tall, "half", "upper"));
        }
    }
    return true;
}

// Vanilla BonemealableBlock (isValidBonemealTarget, isBonemealSuccess, performBonemeal) for
// the block at `pos`: nullopt when it isn't a target, else whether it was used.
std::optional<bool> boneMealBlock(Level& level, const BlockPos& pos) {
    const BlockRegistry& reg = level.registry();
    const Ids& id = ids(reg);
    Xoroshiro& r = level.random();
    const StateId state = level.getBlock(pos);
    const Block& block = reg.blockOf(state);
    const std::string_view name = shortId(block.id), cls = block.className;
    const Plant plant = plantOf(block);
    switch (plant) {
    case Plant::Crop: {
        const int age = reg.intValue(state, "age"), max = cropMaxAge(reg, state);
        if (name == "nether_wart" || age >= max) return std::nullopt;
        int increase = nextIntBetween(r, 2, 5);
        if (name == "beetroots") increase /= 3;
        if (name == "torchflower_crop") increase = 1;
        level.setBlock(pos, cropForAge(reg, state, std::min(age + increase, max)));
        return true;
    }
    case Plant::Stem: {
        const int age = reg.intValue(state, "age");
        if (age == 7) return std::nullopt;
        const int grown = std::min(7, age + nextIntBetween(r, 2, 5));
        const StateId s = withInt(reg, state, "age", grown);
        level.setBlock(pos, s);
        if (grown == 7) stemTick(level, pos, s);
        return true;
    }
    case Plant::Sapling:
        if (r.nextFloat() < 0.45f) advanceTree(level, pos, state);
        return true;
    case Plant::Kelp:
    case Plant::CaveVines: {
        const GrowingPlant g = growingPlant(reg, state);
        if (plant == Plant::CaveVines) { // cave vines bear berries instead
            if (reg.boolValue(state, "berries")) return std::nullopt;
            level.setBlock(pos, withBool(reg, state, "berries", true));
            return true;
        }
        const std::optional<BlockPos> head = is(reg, state, g.head) ? std::optional(pos) : growingPlantHead(level, pos, g);
        if (!head || !canGrowInto(level, g, level.getBlock(head->offset(g.growth)))) return std::nullopt;
        const StateId h = level.getBlock(*head);
        level.setBlockAndUpdate(head->offset(g.growth), grownHead(level, g, h, reg.intValue(h, "age") + 1));
        return true;
    }
    case Plant::Bamboo: {
        int above = bambooAbove(level, pos), below = bambooBelow(level, pos);
        if (above + below + 1 >= 16 || reg.intValue(level.getBlock(pos.above(above)), "stage") == 1) return std::nullopt;
        int height = above + below + 1;
        const int steps = 1 + r.nextInt(2);
        for (int m = 0; m < steps; ++m) {
            const BlockPos top = pos.above(above);
            const StateId s = level.getBlock(top);
            if (height >= 16 || reg.intValue(s, "stage") == 1 || !isAir(level, top.above())) break;
            growBamboo(level, top, s, height);
            ++above;
            ++height;
        }
        return true;
    }
    case Plant::BambooSapling:
        if (!isAir(level, pos.above())) return std::nullopt;
        bambooSaplingGrow(level, pos);
        return true;
    case Plant::BerryBush: {
        const int age = reg.intValue(state, "age");
        if (age >= 3) return std::nullopt;
        level.setBlock(pos, withInt(reg, state, "age", std::min(3, age + 1)));
        return true;
    }
    case Plant::Cocoa: {
        const int age = reg.intValue(state, "age");
        if (age >= 2) return std::nullopt;
        level.setBlock(pos, withInt(reg, state, "age", age + 1));
        return true;
    }
    case Plant::Mushroom: {
        if (r.nextFloat() >= 0.4f) return true;
        // Vanilla growMushroom: the huge mushroom, or the small one back if it didn't fit.
        level.setBlock(pos, BlockRegistry::kAirState);
        if (!placeFeature(level, name == "brown_mushroom" ? "huge_brown_mushroom" : "huge_red_mushroom", pos))
            level.setBlockAndUpdate(pos, state);
        return true;
    }
    case Plant::Grass:
        if (name != "grass_block" || !isAir(level, pos.above())) return std::nullopt;
        boneMealGrass(level, pos);
        return true;
    default: break;
    }
    if (name == "short_grass" || name == "fern") { // into their two-block forms
        const StateId tall = reg.defaultState(name == "fern" ? "large_fern" : "tall_grass");
        if (!isAir(level, pos.above()) || !reg.rules().canSurvive(level, tall, pos)) return std::nullopt;
        level.setBlock(pos, withValue(reg, tall, "half", "lower"));
        level.setBlockAndUpdate(pos.above(), withValue(reg, tall, "half", "upper"));
        level.updateAround(pos, state, level.getBlock(pos));
        return true;
    }
    if (name == "sunflower" || name == "lilac" || name == "rose_bush" || name == "peony") { // a copy pops out
        popItem(level, pos, block);
        return true;
    }
    if (name == "pink_petals" || name == "wildflowers") {
        const int amount = reg.intValue(state, "flower_amount");
        if (amount < 4) level.setBlock(pos, withInt(reg, state, "flower_amount", amount + 1));
        else popItem(level, pos, block);
        return true;
    }
    if (name == "azalea" || name == "flowering_azalea") {
        if (level.fluid(pos.above()).kind != FluidKind::None) return std::nullopt;
        if (r.nextFloat() < 0.45f) growTree(level, pos, state);
        return true;
    }
    if (name == "rooted_dirt") {
        if (!isAir(level, pos.below())) return std::nullopt;
        level.setBlockAndUpdate(pos.below(), reg.block(id.hangingRoots).defaultState);
        return true;
    }
    if (name == "moss_block") {
        if (!isAir(level, pos.above())) return std::nullopt;
        placeFeature(level, "moss_patch_bonemeal", pos.above());
        return true;
    }
    if (name == "seagrass") {
        if (!is(reg, level.getBlock(pos.above()), id.water)) return std::nullopt;
        const StateId tall = reg.block(id.tallSeagrass).defaultState;
        level.setBlock(pos, withValue(reg, tall, "half", "lower"));
        level.setBlock(pos.above(), withValue(reg, tall, "half", "upper"));
        return true;
    }
    if (name == "short_dry_grass") {
        level.setBlockAndUpdate(pos, reg.defaultState("tall_dry_grass"));
        return true;
    }
    if (name == "bush" || name == "firefly_bush" || name == "tall_dry_grass") { // spread beside themselves
        const StateId spawn = name == "tall_dry_grass" ? reg.defaultState("short_dry_grass") : block.defaultState;
        const std::optional<BlockPos> to = spreadableNeighbour(level, pos, spawn);
        if (!to) return std::nullopt;
        level.setBlockAndUpdate(*to, spawn);
        return true;
    }
    (void)cls;
    return std::nullopt;
}

} // namespace

Plant plantOf(const Block& block) {
    const std::string_view cls = block.className, id = shortId(block.id);
    if (cls == "crop") return Plant::Crop;
    if (cls == "stem") return Plant::Stem;
    if (cls == "sapling") return Plant::Sapling;
    if (cls == "sugar_cane") return Plant::SugarCane;
    if (cls == "cactus") return Plant::Cactus;
    if (id == "kelp" || id == "kelp_plant") return Plant::Kelp;
    if (id == "cave_vines" || id == "cave_vines_plant") return Plant::CaveVines;
    if (cls == "bamboo") return Plant::Bamboo;
    if (id == "bamboo_sapling") return Plant::BambooSapling;
    if (cls == "vine") return Plant::Vine;
    if (cls == "berry_bush") return Plant::BerryBush;
    if (cls == "cocoa") return Plant::Cocoa;
    if (id == "brown_mushroom" || id == "red_mushroom") return Plant::Mushroom;
    if (id == "grass_block" || id == "mycelium") return Plant::Grass;
    if (cls == "leaves") return Plant::Leaves;
    return Plant::None;
}

bool plantTicksRandomly(const BlockRegistry& reg, Plant plant, StateId state) {
    switch (plant) {
    case Plant::None: return false;
    case Plant::Crop: return reg.intValue(state, "age") < cropMaxAge(reg, state);
    case Plant::Kelp: return is(reg, state, ids(reg).kelp) && reg.intValue(state, "age") < 25;
    case Plant::CaveVines: return is(reg, state, ids(reg).caveVines) && reg.intValue(state, "age") < 25;
    case Plant::Bamboo: return reg.intValue(state, "stage") == 0;
    case Plant::BerryBush: return reg.intValue(state, "age") < 3;
    case Plant::Cocoa: return reg.intValue(state, "age") < 2;
    case Plant::Leaves: return reg.intValue(state, "distance") == 7 && !reg.boolValue(state, "persistent");
    default: return true;
    }
}

void plantRandomTick(Level& level, Plant plant, const BlockPos& pos, StateId state) {
    switch (plant) {
    case Plant::Crop: cropTick(level, pos, state); break;
    case Plant::Stem: stemTick(level, pos, state); break;
    case Plant::Sapling:
        if (level.rawBrightness(pos.above(), level.skyDarken()) >= 9 && level.random().nextInt(7) == 0)
            advanceTree(level, pos, state);
        break;
    case Plant::SugarCane: sugarCaneTick(level, pos, state); break;
    case Plant::Cactus: cactusTick(level, pos, state); break;
    case Plant::Kelp:
    case Plant::CaveVines: growingPlantTick(level, pos, state); break;
    case Plant::Bamboo: bambooTick(level, pos, state); break;
    case Plant::BambooSapling:
        if (level.random().nextInt(3) == 0 && isAir(level, pos.above()) && level.rawBrightness(pos.above(), 0) >= 9)
            bambooSaplingGrow(level, pos);
        break;
    case Plant::Vine: vineTick(level, pos, state); break;
    case Plant::BerryBush: berryBushTick(level, pos, state); break;
    case Plant::Cocoa: cocoaTick(level, pos, state); break;
    case Plant::Mushroom: mushroomTick(level, pos, state); break;
    case Plant::Grass: grassTick(level, pos, state); break;
    case Plant::Leaves: leavesRandomTick(level, pos, state); break;
    case Plant::None: break;
    }
}

void leavesShapeUpdated(Level& level, const BlockPos& pos, StateId state, StateId neighbor) {
    const BlockRegistry& reg = level.registry();
    const int d = leafDistanceAt(reg, neighbor) + 1;
    if (d != 1 || reg.intValue(state, "distance") != d) level.scheduleTick(pos, reg.info(state).block, 1);
}

void leavesTick(Level& level, const BlockPos& pos, StateId state) {
    const StateId updated = withUpdatedDistance(level, pos, state);
    if (updated != state) level.setBlockAndUpdate(pos, updated);
}

void growingPlantShapeUpdated(Level& level, const BlockPos& pos, StateId state, Direction dir, StateId neighbor) {
    const BlockRegistry& reg = level.registry();
    const GrowingPlant g = growingPlant(reg, state);
    if (dir != g.growth) return;
    const bool continues = is(reg, neighbor, g.head) || is(reg, neighbor, g.body);
    if (is(reg, state, g.head) && continues) {
        // Covered by the plant: a body piece now (cave vines keep their berries).
        StateId body = reg.block(g.body).defaultState;
        if (g.head == ids(reg).caveVines) body = withBool(reg, body, "berries", reg.boolValue(state, "berries"));
        level.setBlockAndUpdate(pos, body);
    } else if (is(reg, state, g.body) && !continues) {
        // The end of the plant: a head with a random age.
        StateId head = withInt(reg, reg.block(g.head).defaultState, "age", level.random().nextInt(25));
        if (g.head == ids(reg).caveVines) head = withBool(reg, head, "berries", reg.boolValue(state, "berries"));
        level.setBlockAndUpdate(pos, head);
    }
}

bool growTree(Level& level, const BlockPos& pos, StateId sapling) {
    const BlockRegistry& reg = level.registry();
    const std::string_view name = shortId(reg.blockOf(sapling).id);
    const TreeGrower* grower = nullptr;
    for (const TreeGrower& g : kTreeGrowers)
        if (g.sapling == name) grower = &g;
    if (!grower || !level.decorator()) return false;
    Xoroshiro& r = level.random();
    // A 2x2 of saplings grows the big tree from its north-west corner.
    const char* mega = grower->secondaryMega && r.nextFloat() < grower->secondaryChance ? grower->secondaryMega : grower->mega;
    if (mega) {
        const uint16_t block = reg.info(sapling).block;
        auto twoByTwo = [&](int dx, int dz) {
            for (int x = 0; x <= 1; ++x)
                for (int z = 0; z <= 1; ++z)
                    if (!is(reg, level.getBlock(pos.offset(dx + x, 0, dz + z)), block)) return false;
            return true;
        };
        for (int i = 0; i >= -1; --i)
            for (int j = 0; j >= -1; --j) {
                if (!twoByTwo(i, j)) continue;
                std::array<StateId, 4> before;
                for (int k = 0; k < 4; ++k) {
                    const BlockPos p = pos.offset(i + (k & 1), 0, j + (k >> 1));
                    before[size_t(k)] = level.getBlock(p);
                    level.setBlock(p, BlockRegistry::kAirState);
                }
                if (placeFeature(level, mega, pos.offset(i, 0, j))) return true;
                for (int k = 0; k < 4; ++k) level.setBlock(pos.offset(i + (k & 1), 0, j + (k >> 1)), before[size_t(k)]);
                return false;
            }
    }
    const bool flowers = hasFlowers(level, pos);
    const char* feature = nullptr;
    if (r.nextFloat() < grower->secondaryChance) {
        if (flowers && grower->secondaryFlowers) feature = grower->secondaryFlowers;
        else if (grower->secondaryTree) feature = grower->secondaryTree;
    }
    if (!feature) feature = flowers && grower->flowers ? grower->flowers : grower->tree;
    if (!feature) return false;
    // The sapling makes way (leaving its water), and comes back if the tree doesn't fit.
    const StateId under = reg.boolValue(sapling, "waterlogged") ? reg.defaultState("water") : BlockRegistry::kAirState;
    level.setBlock(pos, under);
    if (placeFeature(level, feature, pos)) return true;
    level.setBlock(pos, sapling);
    return false;
}

bool applyBoneMeal(Level& level, const BlockPos& pos, Direction face) {
    if (const std::optional<bool> used = boneMealBlock(level, pos)) {
        level.levelEvent(LevelEventType::BoneMeal, pos, 0, 15);
        return *used;
    }
    const BlockPos front = pos.offset(face);
    if (level.sturdy(pos, face) && boneMealWater(level, front, face)) {
        level.levelEvent(LevelEventType::BoneMeal, front, 0, 15);
        return true;
    }
    return false;
}

} // namespace mc::sim
