#include "world/block/BlockRules.h"

#include "resources/ResourcePack.h"

namespace mc {

BlockRules::BlockRules(const BlockRegistry& reg) : reg_(reg), kind_(reg.blockCount(), Kind::Always) {
    auto tag = [&](const char* n) { return reg.tag(n).value_or(TagId(0)); };
    dirt_ = tag("dirt");
    sand_ = tag("sand");
    dry_ = tag("dry_vegetation_may_place_on");
    mushroomGrow_ = tag("mushroom_grow_block");
    bambooOn_ = tag("bamboo_plantable_on");
    snowNever_ = tag("snow_layer_cannot_survive_on");
    snowAlways_ = tag("snow_layer_can_survive_on");
    azalea_ = tag("azalea_grows_on");
    smallDripleafOn_ = tag("small_dripleaf_placeable");
    bigDripleafOn_ = tag("big_dripleaf_placeable");
    jungleLogs_ = tag("jungle_logs");
    auto idx = [&](const char* id) -> uint16_t {
        const Block* b = reg.find(id);
        return b ? b->index : uint16_t(0xFFFF);
    };
    farmland_ = idx("farmland");
    sugarCane_ = idx("sugar_cane");
    cactus_ = idx("cactus");
    ice_ = idx("ice");
    frostedIce_ = idx("frosted_ice");
    magma_ = idx("magma_block");
    kelp_ = idx("kelp");
    kelpPlant_ = idx("kelp_plant");
    caveVines_ = idx("cave_vines");
    caveVinesPlant_ = idx("cave_vines_plant");
    bigDripleaf_ = idx("big_dripleaf");
    bigDripleafStem_ = idx("big_dripleaf_stem");
    smallDripleaf_ = idx("small_dripleaf");
    pointedDripstone_ = idx("pointed_dripstone");
    tallSeagrass_ = idx("tall_seagrass");
    snow_ = idx("snow");
    bamboo_ = idx("bamboo");
    clay_ = idx("clay");

    static const std::unordered_map<std::string_view, Kind> byClass = {
        {"sapling", Kind::Plant},         {"berry_bush", Kind::Plant},           {"flowerbed", Kind::Plant},
        {"double_plant", Kind::Plant},    {"crop", Kind::Farmland},              {"stem", Kind::Farmland},
        {"pitcher_crop", Kind::Farmland}, {"cactus", Kind::Cactus},              {"sugar_cane", Kind::SugarCane},
        {"water_plant", Kind::Seagrass},  {"tall_water_plant", Kind::Seagrass},  {"kelp", Kind::Kelp},
        {"coral", Kind::Coral},           {"coral_wall_fan", Kind::WallCoral},   {"sea_pickle", Kind::SeaPickle},
        {"bamboo", Kind::Bamboo},         {"cocoa", Kind::Cocoa},                {"vine", Kind::Vine},
        {"multiface", Kind::Multiface},   {"snow_layer", Kind::Snow},            {"cave_vines", Kind::CaveVines},
        {"cave_vines_plant", Kind::CaveVines}, {"small_dripleaf", Kind::SmallDripleaf},
        {"big_dripleaf", Kind::BigDripleaf}, {"big_dripleaf_stem", Kind::BigDripleafStem},
        {"pointed_dripstone", Kind::PointedDripstone}, {"leaf_litter", Kind::LeafLitter},
        {"torch", Kind::FloorSupport},    {"redstone_torch", Kind::FloorSupport}, {"rail", Kind::FloorSupport},
        {"powered_rail", Kind::FloorSupport}, {"pressure_plate", Kind::FloorSupport},
        {"weighted_pressure_plate", Kind::FloorSupport}, {"door", Kind::FloorSupport},
        {"redstone_wire", Kind::FloorSupport}, {"repeater", Kind::FloorSupport}, {"comparator", Kind::FloorSupport},
        {"sign", Kind::FloorSupport},     {"banner", Kind::FloorSupport},       {"candle", Kind::FloorSupport},
        {"wall_torch", Kind::WallAttached}, {"redstone_wall_torch", Kind::WallAttached}, {"ladder", Kind::WallAttached},
        {"wall_sign", Kind::WallAttached}, {"wall_banner", Kind::WallAttached}, {"wall_hanging_sign", Kind::WallAttached},
        {"button", Kind::FaceAttached},   {"lever", Kind::FaceAttached},
    };
    static const std::unordered_map<std::string_view, Kind> byId = {
        {"short_grass", Kind::Plant},       {"fern", Kind::Plant},           {"bush", Kind::Plant},
        {"firefly_bush", Kind::Plant},      {"dandelion", Kind::Plant},      {"poppy", Kind::Plant},
        {"blue_orchid", Kind::Plant},       {"allium", Kind::Plant},         {"azure_bluet", Kind::Plant},
        {"red_tulip", Kind::Plant},         {"orange_tulip", Kind::Plant},   {"white_tulip", Kind::Plant},
        {"pink_tulip", Kind::Plant},        {"oxeye_daisy", Kind::Plant},    {"cornflower", Kind::Plant},
        {"lily_of_the_valley", Kind::Plant}, {"torchflower", Kind::Plant},   {"open_eyeblossom", Kind::Plant},
        {"closed_eyeblossom", Kind::Plant}, {"wither_rose", Kind::Plant},    {"dead_bush", Kind::DryPlant},
        {"short_dry_grass", Kind::DryPlant}, {"tall_dry_grass", Kind::DryPlant}, {"brown_mushroom", Kind::Mushroom},
        {"red_mushroom", Kind::Mushroom},   {"lily_pad", Kind::LilyPad},     {"hanging_roots", Kind::HangingFromCeiling},
        {"spore_blossom", Kind::SporeBlossom}, {"moss_carpet", Kind::Carpet}, {"pale_moss_carpet", Kind::Carpet},
        {"azalea", Kind::Azalea},           {"flowering_azalea", Kind::Azalea},
        {"kelp_plant", Kind::Kelp},         {"cactus_flower", Kind::CactusFlower},
    };
    for (size_t b = 0; b < reg.blockCount(); ++b) {
        const Block& block = reg.block(b);
        const std::string_view name = shortId(block.id);
        if (auto it = byId.find(name); it != byId.end()) kind_[b] = it->second;
        else if (auto c = byClass.find(block.className); c != byClass.end()) kind_[b] = c->second;
        else if (name.ends_with("_carpet")) kind_[b] = Kind::Carpet;
    }
}

bool BlockRules::snowCanSurvive(const BlockGetter& level, const BlockPos& pos) const {
    const BlockPos below = pos.below();
    const StateId s = level.getBlock(below);
    if (reg_.hasTag(s, snowNever_)) return false;
    if (reg_.hasTag(s, snowAlways_)) return true;
    if (level.sturdy(below, Direction::Up)) return true;
    return reg_.info(s).block == snow_ && reg_.intValue(s, "layers") == 8;
}

bool BlockRules::supportedFromColumn(StateId state) const {
    switch (kind_[reg_.info(state).block]) {
    case Kind::Always:
    case Kind::SugarCane:
    case Kind::Cactus:
    case Kind::WallCoral:
    case Kind::Cocoa:
    case Kind::Vine:
    case Kind::Multiface:
    case Kind::WallAttached:
    case Kind::FaceAttached: return false;
    default: return true;
    }
}

bool BlockRules::canSurvive(const BlockGetter& level, StateId state, const BlockPos& pos) const {
    const StateInfo& si = reg_.info(state);
    const Kind kind = kind_[si.block];
    if (kind == Kind::Always) return true;
    const BlockPos below = pos.below(), above = pos.above();
    const StateId belowState = level.getBlock(below);
    const StateInfo& bi = reg_.info(belowState);
    auto belowIs = [&](uint16_t block) { return bi.block == block; };
    auto onDirt = [&] { return reg_.hasTag(belowState, dirt_) || belowIs(farmland_); };

    // Upper halves rest on their own lower half.
    if (reg_.value(state, "half") == "upper") return bi.block == si.block && reg_.value(belowState, "half") == "lower";

    switch (kind) {
    case Kind::Always: return true;
    case Kind::Plant: return onDirt();
    case Kind::Farmland: return belowIs(farmland_);
    case Kind::DryPlant: return reg_.hasTag(belowState, dry_);
    case Kind::Mushroom: return reg_.hasTag(belowState, mushroomGrow_) || (bi.flags & kOpaqueCube);
    case Kind::Azalea: return onDirt() || belowIs(clay_) || reg_.hasTag(belowState, azalea_);
    case Kind::SugarCane: {
        if (belowIs(sugarCane_)) return true;
        if (!reg_.hasTag(belowState, dirt_) && !reg_.hasTag(belowState, sand_)) return false;
        for (Direction d : kHorizontal) {
            const BlockPos n = below.offset(d);
            if (level.fluid(n).kind == FluidKind::Water || level.info(n).block == frostedIce_) return true;
        }
        return false;
    }
    case Kind::Cactus: {
        for (Direction d : kHorizontal) {
            const StateInfo& n = level.info(pos.offset(d));
            if ((n.flags & kBlocksMotion) || n.fluid.kind == FluidKind::Lava) return false;
        }
        return (belowIs(cactus_) || reg_.hasTag(belowState, sand_)) && level.fluid(above).empty();
    }
    case Kind::LilyPad: return (bi.fluid.kind == FluidKind::Water && bi.fluid.source()) || belowIs(ice_);
    case Kind::Seagrass: return level.sturdy(below, Direction::Up) && !belowIs(magma_);
    case Kind::Kelp:
        return !belowIs(magma_) && (belowIs(kelp_) || belowIs(kelpPlant_) || level.sturdy(below, Direction::Up));
    case Kind::Coral: return level.sturdy(below, Direction::Up);
    case Kind::WallCoral: {
        const std::string_view f = reg_.value(state, "facing");
        Direction facing = Direction::North;
        for (Direction d : kHorizontal)
            if (f == std::array<std::string_view, 6>{"down", "up", "north", "south", "west", "east"}[size_t(d)]) facing = d;
        return level.sturdy(pos.offset(opposite(facing)), facing);
    }
    case Kind::SeaPickle: return level.sturdy(below, Direction::Up) || (bi.flags & kBlocksMotion);
    case Kind::Bamboo: return reg_.hasTag(belowState, bambooOn_);
    case Kind::Cocoa: {
        const std::string_view f = reg_.value(state, "facing");
        for (Direction d : kHorizontal)
            if (f == std::array<std::string_view, 6>{"down", "up", "north", "south", "west", "east"}[size_t(d)])
                return level.hasTag(pos.offset(d), jungleLogs_);
        return false;
    }
    case Kind::Vine: {
        static constexpr std::pair<const char*, Direction> faces[] = {{"north", Direction::North}, {"south", Direction::South},
                                                                      {"west", Direction::West},   {"east", Direction::East},
                                                                      {"up", Direction::Up}};
        for (const auto& [prop, d] : faces)
            if (reg_.boolValue(state, prop) && level.sturdy(pos.offset(d), opposite(d))) return true;
        return false;
    }
    case Kind::Multiface: {
        static constexpr std::pair<const char*, Direction> faces[] = {
            {"down", Direction::Down}, {"up", Direction::Up},     {"north", Direction::North},
            {"south", Direction::South}, {"west", Direction::West}, {"east", Direction::East}};
        bool any = false;
        for (const auto& [prop, d] : faces)
            if (reg_.boolValue(state, prop)) {
                if (!level.sturdy(pos.offset(d), opposite(d))) return false;
                any = true;
            }
        return any;
    }
    case Kind::Snow: return snowCanSurvive(level, pos);
    case Kind::CaveVines: {
        const uint16_t a = level.info(above).block;
        return a == caveVines_ || a == caveVinesPlant_ || level.sturdy(above, Direction::Down);
    }
    case Kind::HangingFromCeiling: return level.sturdy(above, Direction::Down);
    case Kind::SporeBlossom: return level.sturdy(above, Direction::Down) && level.fluid(pos).empty();
    case Kind::SmallDripleaf:
        return reg_.hasTag(belowState, smallDripleafOn_) || (level.isWaterSource(pos) && onDirt());
    case Kind::BigDripleaf:
        return belowIs(bigDripleaf_) || belowIs(bigDripleafStem_) || reg_.hasTag(belowState, bigDripleafOn_);
    case Kind::BigDripleafStem: return belowIs(bigDripleafStem_) || reg_.hasTag(belowState, bigDripleafOn_);
    case Kind::PointedDripstone: {
        const bool up = reg_.value(state, "vertical_direction") == "up";
        const BlockPos base = up ? below : above;
        const StateId b = level.getBlock(base);
        if (level.sturdy(base, up ? Direction::Up : Direction::Down)) return true;
        return reg_.info(b).block == pointedDripstone_ && reg_.value(b, "vertical_direction") == (up ? "up" : "down");
    }
    case Kind::DoublePlantTop: return bi.block == si.block;
    case Kind::Carpet: return !(bi.flags & kAir);
    case Kind::LeafLitter: return level.sturdy(below, Direction::Up);
    case Kind::CactusFlower: return belowIs(cactus_) || belowIs(farmland_) || level.sturdy(below, Direction::Up);
    case Kind::FloorSupport: return level.sturdy(below, Direction::Up) || (bi.flags & kBlocksMotion);
    case Kind::WallAttached: {
        // `facing` points away from the wall the block hangs on.
        const Direction f = directionFromName(reg_.value(state, "facing")).value_or(Direction::North);
        return level.sturdy(pos.offset(opposite(f)), f);
    }
    case Kind::FaceAttached: {
        const std::string_view face = reg_.value(state, "face");
        if (face == "floor") return level.sturdy(below, Direction::Up);
        if (face == "ceiling") return level.sturdy(above, Direction::Down);
        const Direction f = directionFromName(reg_.value(state, "facing")).value_or(Direction::North);
        return level.sturdy(pos.offset(opposite(f)), f);
    }
    }
    return true;
}

} // namespace mc
