#include "world/sim/Simulation.h"

#include <optional>
#include <vector>

#include "entity/FallingBlockEntity.h"
#include "resources/ResourcePack.h"
#include "world/block/BlockBehavior.h"
#include "world/chunk/Chunk.h"
#include "world/level/Level.h"
#include "world/sim/Fire.h"
#include "world/sim/Fluids.h"
#include "world/sim/Plants.h"
#include "world/sim/Weathering.h"

namespace mc::sim {
namespace {

// What each block is to the simulation, cached per registry.
enum class Kind : uint8_t {
    None,
    Liquid,        // water and lava blocks
    Waterloggable, // blocks with a waterlogged property
    WaterPlant,    // kelp, seagrass: always in a water source
    Falling,       // sand, gravel, concrete powder, anvils (vanilla FallingBlock)
    Leaves,        // waterloggable, and keeping their distance to a log
    CaveVines,     // head and body
    Fire,          // fire and soul fire
    Farmland,
    Sponge,
};

struct Kinds {
    std::vector<Kind> byBlock;
    std::vector<Plant> plant;
    std::vector<Weathering> weathering;
    explicit Kinds(const BlockRegistry& reg) {
        byBlock.resize(reg.blockCount(), Kind::None);
        plant.resize(reg.blockCount(), Plant::None);
        weathering.resize(reg.blockCount(), Weathering::None);
        for (size_t i = 0; i < reg.blockCount(); ++i) {
            plant[i] = plantOf(reg.block(i));
            weathering[i] = weatheringOf(reg.block(i));
        }
        for (size_t i = 0; i < reg.blockCount(); ++i) {
            const Block& b = reg.block(i);
            const std::string_view id = shortId(b.id);
            Kind k = Kind::None;
            if (b.className == "liquid") k = Kind::Liquid;
            else if (b.className == "water_plant" || b.className == "tall_water_plant" || b.className == "kelp" ||
                     id == "kelp_plant")
                k = Kind::WaterPlant;
            else if (FallingBlockEntity::kindOf(b) != FallingBlockEntity::Kind::None) k = Kind::Falling;
            else if (b.className == "leaves") k = Kind::Leaves;
            else if (id == "cave_vines" || id == "cave_vines_plant") k = Kind::CaveVines;
            else if (b.className == "fire" || id == "soul_fire") k = Kind::Fire;
            else if (b.className == "farmland") k = Kind::Farmland;
            else if (id == "sponge") k = Kind::Sponge;
            else if (!reg.value(b.defaultState, "waterlogged").empty()) k = Kind::Waterloggable;
            byBlock[i] = k;
        }
    }
};

const Kinds& kinds(const BlockRegistry& reg) {
    static thread_local const BlockRegistry* owner = nullptr;
    static thread_local std::optional<Kinds> k;
    if (owner != &reg) {
        k.emplace(reg);
        owner = &reg;
    }
    return *k;
}

Kind kindOf(const BlockRegistry& reg, StateId s) { return kinds(reg).byBlock[reg.info(s).block]; }

// Vanilla FallingBlock.getDelayAfterPlace: falling blocks check their support 2 ticks on.
constexpr int kFallDelay = 2;

// Vanilla LiquidBlock.onPlace/neighborChanged: lava may harden; otherwise the fluid gets a tick.
void liquidUpdate(Level& level, const BlockPos& pos, StateId state) {
    if (lavaHardens(level, pos, state)) return;
    const Fluid f = fluidOf(level.registry(), state);
    level.scheduleFluidTick(pos, fluidTickType(f.kind, f.source), fluidTickDelay(f.kind));
}

} // namespace

uint16_t fluidTickType(FluidKind kind, bool source) {
    if (kind == FluidKind::None) return 0;
    return uint16_t((kind == FluidKind::Lava ? 3 : 1) + (source ? 0 : 1));
}

uint16_t fluidTickTypeOf(const BlockRegistry& registry, StateId state) {
    const Fluid f = fluidOf(registry, state);
    return fluidTickType(f.kind, f.source);
}

void onPlace(Level& level, const BlockPos& pos, StateId state, StateId old) {
    const BlockRegistry& reg = level.registry();
    switch (kindOf(reg, state)) {
    case Kind::Liquid: liquidUpdate(level, pos, state); break;
    case Kind::Fire: fireOnPlace(level, pos, state, old); break;
    case Kind::Sponge:
        if (reg.info(old).block != reg.info(state).block) spongeUpdate(level, pos, state);
        break;
    case Kind::Falling:
        // ConcretePowderBlock.getStateForPlacement: placed into or beside water, it sets.
        if (FallingBlockEntity::kindOf(reg.blockOf(state)) == FallingBlockEntity::Kind::ConcretePowder &&
            (reg.info(old).fluid.kind == FluidKind::Water || FallingBlockEntity::touchesWater(level, pos))) {
            level.setBlockAndUpdate(pos, FallingBlockEntity::concreteOf(reg, state));
            break;
        }
        level.scheduleTick(pos, reg.info(state).block, kFallDelay);
        break;
    default: break;
    }
}

void neighborChanged(Level& level, const BlockPos& pos, StateId state, const BlockPos& from) {
    (void)from;
    const BlockRegistry& reg = level.registry();
    switch (kindOf(reg, state)) {
    case Kind::Liquid: liquidUpdate(level, pos, state); break;
    case Kind::Sponge: spongeUpdate(level, pos, state); break;
    default: break;
    }
}

void shapeUpdated(Level& level, const BlockPos& pos, StateId state, Direction dir, StateId neighbor) {
    const BlockRegistry& reg = level.registry();
    switch (kindOf(reg, state)) {
    case Kind::Liquid: {
        // Vanilla LiquidBlock.updateShape: a source here or there wakes the fluid.
        const Fluid f = fluidOf(reg, state);
        if (f.source || fluidOf(reg, neighbor).source)
            level.scheduleFluidTick(pos, fluidTickType(f.kind, f.source), fluidTickDelay(f.kind));
        break;
    }
    case Kind::Leaves:
        leavesShapeUpdated(level, pos, state, neighbor);
        [[fallthrough]];
    case Kind::Waterloggable:
        if (!reg.boolValue(state, "waterlogged")) break;
        level.scheduleFluidTick(pos, fluidTickType(FluidKind::Water, true), fluidTickDelay(FluidKind::Water));
        break;
    case Kind::WaterPlant:
        level.scheduleFluidTick(pos, fluidTickType(FluidKind::Water, true), fluidTickDelay(FluidKind::Water));
        if (kinds(reg).plant[reg.info(state).block] == Plant::Kelp) growingPlantShapeUpdated(level, pos, state, dir, neighbor);
        break;
    case Kind::CaveVines: growingPlantShapeUpdated(level, pos, state, dir, neighbor); break;
    case Kind::Farmland: farmlandShapeUpdated(level, pos, state, dir); break;
    case Kind::Falling:
        // ConcretePowderBlock.updateShape: water touching it sets it at once.
        if (FallingBlockEntity::kindOf(reg.blockOf(state)) == FallingBlockEntity::Kind::ConcretePowder &&
            FallingBlockEntity::touchesWater(level, pos)) {
            level.setBlockAndUpdate(pos, FallingBlockEntity::concreteOf(reg, state));
            break;
        }
        level.scheduleTick(pos, reg.info(state).block, kFallDelay);
        break;
    default: break;
    }
}

void blockTick(Level& level, const BlockPos& pos, StateId state) {
    const BlockRegistry& reg = level.registry();
    if (kindOf(reg, state) == Kind::Falling) {
        // FallingBlock.tick: nothing under it, so it falls.
        if (FallingBlockEntity::isFree(reg, level.getBlock(pos.below())) && pos.y >= kWorldMinY)
            FallingBlockEntity::fall(level, pos, state);
        return;
    }
    switch (kindOf(reg, state)) {
    case Kind::Leaves: leavesTick(level, pos, state); return;
    case Kind::Fire: fireTick(level, pos, state); return;
    case Kind::Farmland: farmlandTick(level, pos, state); return;
    default: break;
    }
    const StateId after = scheduledTickResult(level.registry(), level, state, pos);
    if (after != state) level.setBlockAndUpdate(pos, after);
}

void fluidTick(Level& level, const BlockPos& pos, StateId state, uint16_t type) {
    if (fluidTickTypeOf(level.registry(), state) != type) return; // the fluid changed meanwhile
    sim::fluidTick(level, pos, state);
}

bool randomlyTicks(const BlockRegistry& registry, StateId state) {
    const Kinds& k = kinds(registry);
    const uint16_t b = registry.info(state).block;
    if (k.plant[b] != Plant::None) return plantTicksRandomly(registry, k.plant[b], state);
    if (k.weathering[b] != Weathering::None) return weatheringTicksRandomly(registry, k.weathering[b], state);
    return registry.info(state).fluid.kind == FluidKind::Lava; // vanilla LavaFluid.isRandomlyTicking
}

void randomTick(Level& level, const BlockPos& pos, StateId state) {
    const BlockRegistry& reg = level.registry();
    const Kinds& k = kinds(reg);
    const uint16_t b = reg.info(state).block;
    if (k.plant[b] != Plant::None) plantRandomTick(level, k.plant[b], pos, state);
    else if (k.weathering[b] != Weathering::None) weatheringRandomTick(level, k.weathering[b], pos, state);
    else if (reg.info(state).fluid.kind == FluidKind::Lava) lavaRandomTick(level, pos);
}

void postProcessChunk(Level& level, ChunkPos chunk) {
    const BlockRegistry& reg = level.registry();
    const Chunk* c = level.chunks().chunk(chunk);
    if (!c) return;
    for (int si = 0; si < kSectionCount; ++si) {
        const Section& section = c->sections[size_t(si)];
        if (!section.blocks) continue;
        const int y0 = sectionMinY(si);
        for (int i = 0; i < 4096; ++i) {
            const StateId s = section.blocks->states[size_t(i)];
            if (reg.info(s).fluid.empty()) continue;
            const BlockPos p(chunk.x * 16 + (i & 15), y0 + (i >> 8), chunk.z * 16 + ((i >> 4) & 15));
            for (Direction d : {Direction::Down, Direction::North, Direction::East, Direction::South, Direction::West})
                if (canFlowOut(level, p, s, d)) {
                    const Fluid f = fluidOf(reg, s);
                    level.scheduleFluidTick(p, fluidTickType(f.kind, f.source), 1);
                    break;
                }
        }
    }
}

} // namespace mc::sim
