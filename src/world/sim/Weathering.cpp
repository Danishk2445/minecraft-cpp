#include "world/sim/Weathering.h"

#include <deque>
#include <string>
#include <unordered_set>

#include "entity/Player.h"
#include "resources/ResourcePack.h"
#include "world/block/BlockRules.h"
#include "world/level/Level.h"

namespace mc::sim {
namespace {

StateId withValue(const BlockRegistry& reg, StateId s, std::string_view prop, std::string_view value) {
    return reg.with(s, prop, value).value_or(s);
}

// ---------------------------------------------------------------- farmland

// Vanilla FarmBlock.isNearWater: water within 4 blocks sideways, level with it or one up.
bool nearWater(const Level& level, const BlockPos& pos) {
    for (int y = 0; y <= 1; ++y)
        for (int z = -4; z <= 4; ++z)
            for (int x = -4; x <= 4; ++x)
                if (level.fluid(pos.offset(x, y, z)).kind == FluidKind::Water) return true;
    return false;
}

// Vanilla FarmBlock.canSurvive: nothing solid on top (fence gates are fine).
bool farmlandSurvives(const Level& level, const BlockPos& pos) {
    const BlockRegistry& reg = level.registry();
    const StateId above = level.getBlock(pos.above());
    return !(reg.info(above).flags & kBlocksMotion) || reg.blockOf(above).className == "fence_gate";
}

void turnToDirt(Level& level, const BlockPos& pos) {
    // Vanilla pushEntitiesUp: whoever stands on it rises the 1/16 the block grows.
    if (Player* p = level.player(); p && !p->noPhysics) {
        const AABB top(pos.x, pos.y + 0.9375, pos.z, pos.x + 1.0, pos.y + 1.0, pos.z + 1.0);
        if (p->box().intersects(top) && p->pos.y < pos.y + 1.0) p->pos.y = pos.y + 1.0;
    }
    level.setBlockAndUpdate(pos, level.registry().defaultState("dirt"));
}

void farmlandRandomTick(Level& level, const BlockPos& pos, StateId state) {
    const BlockRegistry& reg = level.registry();
    const int moisture = reg.intValue(state, "moisture");
    if (!nearWater(level, pos) && !level.isRainingAt(pos.above())) {
        if (moisture > 0) {
            level.setBlock(pos, withValue(reg, state, "moisture", std::to_string(moisture - 1)));
        } else {
            // Bare and dry: back to dirt, unless a crop keeps it.
            static thread_local const BlockRegistry* owner = nullptr;
            static thread_local TagId maintains = 0;
            if (owner != &reg) {
                maintains = reg.tag("maintains_farmland").value_or(TagId(0));
                owner = &reg;
            }
            if (!reg.hasTag(level.getBlock(pos.above()), maintains)) turnToDirt(level, pos);
        }
    } else if (moisture < 7) {
        level.setBlock(pos, withValue(reg, state, "moisture", "7"));
    }
}

// ---------------------------------------------------------------- copper

// Weathering stage of a copper block id (0 unaffected .. 3 oxidized), -1 for blocks that
// don't weather (waxed, ores, raw copper).
int copperStage(std::string_view id) {
    if (id.starts_with("waxed_") || id.find("copper") == std::string_view::npos || id.ends_with("_ore") ||
        id == "raw_copper_block")
        return -1;
    if (id.starts_with("exposed_")) return 1;
    if (id.starts_with("weathered_")) return 2;
    if (id.starts_with("oxidized_")) return 3;
    return 0;
}

void copperRandomTick(Level& level, const BlockPos& pos, StateId state) {
    // Vanilla ChangeOverTimeBlock: 1 in ~17.6 ticks, it may age; less weathered copper
    // within 4 blocks (Manhattan) holds it back, more weathered copper speeds it up.
    const BlockRegistry& reg = level.registry();
    Xoroshiro& r = level.random();
    if (r.nextFloat() >= 0.05688889f) return;
    const int stage = copperStage(shortId(reg.blockOf(state).id));
    int same = 0, older = 0;
    for (int dy = -4; dy <= 4; ++dy)
        for (int dz = -4; dz <= 4; ++dz)
            for (int dx = -4; dx <= 4; ++dx) {
                if (std::abs(dx) + std::abs(dy) + std::abs(dz) > 4 || (dx == 0 && dy == 0 && dz == 0)) continue;
                const int other = copperStage(shortId(reg.blockOf(level.getBlock(pos.offset(dx, dy, dz))).id));
                if (other < 0) continue;
                if (other < stage) return;
                if (other > stage) ++older;
                else ++same;
            }
    const float f = float(older + 1) / float(older + same + 1);
    const float chance = f * f * (stage == 0 ? 0.75f : 1.0f);
    if (r.nextFloat() >= chance) return;
    const Block* next = reg.find(copperNext(shortId(reg.blockOf(state).id)));
    if (!next) return;
    // The same shape and facing (stairs, slabs, doors, bulbs keep their properties).
    StateId aged = next->defaultState;
    const Block& from = reg.blockOf(state);
    for (uint16_t p : from.props) {
        const std::string& name = reg.property(p).name;
        if (auto s = reg.with(aged, name, reg.value(state, name))) aged = *s;
    }
    level.setBlockAndUpdate(pos, aged);
}

// ---------------------------------------------------------------- amethyst

void buddingAmethystTick(Level& level, const BlockPos& pos) {
    const BlockRegistry& reg = level.registry();
    Xoroshiro& r = level.random();
    if (r.nextInt(5) != 0) return;
    const Direction d = kDirections[size_t(r.nextInt(6))];
    const BlockPos at = pos.offset(d);
    const StateId there = level.getBlock(at);
    const std::string_view id = shortId(reg.blockOf(there).id);
    const bool sameFacing = reg.value(there, "facing") == directionName(d);
    const char* next = nullptr;
    if ((reg.info(there).flags & kAir) || (id == "water" && reg.info(there).fluid.source())) next = "small_amethyst_bud";
    else if (id == "small_amethyst_bud" && sameFacing) next = "medium_amethyst_bud";
    else if (id == "medium_amethyst_bud" && sameFacing) next = "large_amethyst_bud";
    else if (id == "large_amethyst_bud" && sameFacing) next = "amethyst_cluster";
    if (!next) return;
    StateId bud = withValue(reg, reg.defaultState(next), "facing", directionName(d));
    bud = withValue(reg, bud, "waterlogged", reg.info(there).fluid.kind == FluidKind::Water && reg.info(there).fluid.source()
                                                 ? "true"
                                                 : "false");
    level.setBlockAndUpdate(at, bud);
}

} // namespace

Weathering weatheringOf(const Block& block) {
    const std::string_view id = shortId(block.id);
    if (block.className == "farmland") return Weathering::Farmland;
    if (id == "ice") return Weathering::Ice;
    if (block.className == "snow_layer") return Weathering::SnowLayer;
    if (id == "budding_amethyst") return Weathering::BuddingAmethyst;
    if (copperStage(id) >= 0 && copperStage(id) < 3) return Weathering::Copper;
    return Weathering::None;
}

bool weatheringTicksRandomly(const BlockRegistry& registry, Weathering kind, StateId state) {
    (void)registry, (void)state;
    return kind != Weathering::None;
}

void weatheringRandomTick(Level& level, Weathering kind, const BlockPos& pos, StateId state) {
    const BlockRegistry& reg = level.registry();
    switch (kind) {
    case Weathering::Farmland: farmlandRandomTick(level, pos, state); break;
    case Weathering::Ice:
        // IceBlock: block light above 11 less its light dampening (1) melts it.
        if ((level.lightAt(pos) & 15) > 11 - 1) level.setBlockAndUpdate(pos, reg.defaultState("water"));
        break;
    case Weathering::SnowLayer:
        if ((level.lightAt(pos) & 15) > 11) {
            level.dropResources(pos, state);
            level.setBlockAndUpdate(pos, BlockRegistry::kAirState);
        }
        break;
    case Weathering::Copper: copperRandomTick(level, pos, state); break;
    case Weathering::BuddingAmethyst: buddingAmethystTick(level, pos); break;
    case Weathering::None: break;
    }
}

void farmlandShapeUpdated(Level& level, const BlockPos& pos, StateId state, Direction dir) {
    if (dir == Direction::Up && !farmlandSurvives(level, pos)) level.scheduleTick(pos, level.registry().info(state).block, 1);
}

void farmlandTick(Level& level, const BlockPos& pos, StateId) {
    if (!farmlandSurvives(level, pos)) turnToDirt(level, pos);
}

void trampleFarmland(Level& level, const BlockPos& pos, double fallDistance) {
    if (level.registry().blockOf(level.getBlock(pos)).className != "farmland") return;
    if (level.random().nextFloat() < float(fallDistance) - 0.5f) turnToDirt(level, pos);
}

void spongeUpdate(Level& level, const BlockPos& pos, StateId state) {
    const BlockRegistry& reg = level.registry();
    if (shortId(reg.blockOf(state).id) != "sponge") return;
    // Vanilla queues the neighbor updates its own removals cause until it is done (and wet).
    static thread_local bool absorbing = false;
    if (absorbing) return;
    absorbing = true;
    struct Reset {
        ~Reset() { absorbing = false; }
    } reset;
    // Vanilla BlockPos.breadthFirstTraversal: depth 6, at most 65 accepted (the sponge too).
    std::deque<std::pair<BlockPos, int>> queue{{pos, 0}};
    std::unordered_set<BlockPos> seen;
    int accepted = 0;
    while (!queue.empty()) {
        const auto [p, depth] = queue.front();
        queue.pop_front();
        if (!seen.insert(p).second) continue;
        if (p != pos) {
            const StateId s = level.getBlock(p);
            const StateInfo& info = reg.info(s);
            if (info.fluid.kind != FluidKind::Water) continue;
            const Block& b = reg.blockOf(s);
            const std::string_view id = shortId(b.id);
            if (!reg.value(s, "waterlogged").empty()) {
                level.setBlockAndUpdate(p, withValue(reg, s, "waterlogged", "false"));
            } else if (b.className == "liquid" || id == "bubble_column") {
                level.setBlockAndUpdate(p, BlockRegistry::kAirState);
            } else if (id == "kelp" || id == "kelp_plant" || id == "seagrass" || id == "tall_seagrass") {
                level.dropResources(p, s);
                level.setBlockAndUpdate(p, BlockRegistry::kAirState);
            } else {
                continue;
            }
        }
        if (++accepted >= 65) break;
        if (depth < 6)
            for (Direction d : kDirections) queue.emplace_back(p.offset(d), depth + 1);
    }
    if (accepted > 1) {
        level.setBlock(pos, reg.defaultState("wet_sponge"));
        level.levelEvent(LevelEventType::BlockBroken, pos, reg.defaultState("water"));
    }
}

std::string copperNext(std::string_view id) {
    switch (copperStage(id)) {
    case 0: return id == "copper_block" ? "exposed_copper" : "exposed_" + std::string(id);
    case 1: return "weathered_" + std::string(id.substr(8));
    case 2: return "oxidized_" + std::string(id.substr(10));
    default: return {};
    }
}

} // namespace mc::sim
