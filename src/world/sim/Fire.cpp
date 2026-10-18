#include "world/sim/Fire.h"

#include <algorithm>
#include <string>

#include "entity/PrimedTnt.h"
#include "resources/ResourcePack.h"
#include "world/block/BlockBehavior.h"
#include "world/level/Level.h"

namespace mc::sim {
namespace {

StateId withAge(const BlockRegistry& reg, StateId fire, int age) {
    return reg.with(fire, "age", std::to_string(age)).value_or(fire);
}

// Vanilla getStateWithAge: the fire for the cell, aged (soul fire has no age).
StateId fireWithAge(const Level& level, const BlockPos& pos, int age) {
    const BlockRegistry& reg = level.registry();
    const StateId s = fireStateAt(reg, level, pos);
    return reg.blockOf(s).className == "fire" ? withAge(reg, s, age) : s;
}

int burnOdds(const BlockRegistry& reg, StateId s) { return reg.boolValue(s, "waterlogged") ? 0 : reg.blockOf(s).burnOdds; }
int igniteOdds(const BlockRegistry& reg, StateId s) { return reg.boolValue(s, "waterlogged") ? 0 : reg.blockOf(s).igniteOdds; }

// Vanilla getIgniteOdds(level, pos): an empty cell catches as well as its most flammable neighbor.
int igniteOddsAt(const Level& level, const BlockPos& pos) {
    const BlockRegistry& reg = level.registry();
    if (!(reg.info(level.getBlock(pos)).flags & kAir)) return 0;
    int odds = 0;
    for (Direction d : kDirections) odds = std::max(odds, igniteOdds(reg, level.getBlock(pos.offset(d))));
    return odds;
}

bool nearRain(const Level& level, const BlockPos& pos) {
    if (level.isRainingAt(pos)) return true;
    for (Direction d : kHorizontal)
        if (level.isRainingAt(pos.offset(d))) return true;
    return false;
}

bool isTnt(const BlockRegistry& reg, StateId s) { return shortId(reg.blockOf(s).id) == "tnt"; }

// Vanilla checkBurnOut: the neighbor burns away by its burn odds, sometimes into more fire.
void checkBurnOut(Level& level, const BlockPos& pos, int chance, int age) {
    const BlockRegistry& reg = level.registry();
    Xoroshiro& r = level.random();
    const StateId s = level.getBlock(pos);
    if (r.nextInt(chance) >= burnOdds(reg, s)) return;
    if (r.nextInt(age + 10) < 5 && !level.isRainingAt(pos)) {
        const int a = std::min(age + r.nextInt(5) / 4, 15);
        level.setBlockAndUpdate(pos, fireWithAge(level, pos, a));
    } else {
        level.setBlockAndUpdate(pos, reg.boolValue(s, "waterlogged") ? reg.defaultState("water") : BlockRegistry::kAirState);
    }
    if (isTnt(reg, s)) PrimedTnt::prime(level, pos);
}

void removeFire(Level& level, const BlockPos& pos) { level.setBlockAndUpdate(pos, BlockRegistry::kAirState); }

} // namespace

int fireTickDelay(Level& level) { return 30 + level.random().nextInt(10); }

void fireOnPlace(Level& level, const BlockPos& pos, StateId state, StateId old) {
    const BlockRegistry& reg = level.registry();
    if (reg.info(old).block == reg.info(state).block) return;
    if (!fireCanSurvive(reg, level, state, pos)) {
        removeFire(level, pos);
        return;
    }
    if (reg.blockOf(state).className == "fire") level.scheduleTick(pos, reg.info(state).block, fireTickDelay(level));
}

void fireTick(Level& level, const BlockPos& pos, StateId state) {
    const BlockRegistry& reg = level.registry();
    Xoroshiro& r = level.random();
    level.scheduleTick(pos, reg.info(state).block, fireTickDelay(level));
    if (!level.doFireTick) return;
    if (!fireCanSurvive(reg, level, state, pos)) removeFire(level, pos);
    static thread_local const BlockRegistry* owner = nullptr;
    static thread_local TagId infiniburn = 0;
    if (owner != &reg) {
        infiniburn = reg.tag("infiniburn_overworld").value_or(TagId(0));
        owner = &reg;
    }
    const StateId below = level.getBlock(pos.below());
    const bool forever = reg.hasTag(below, infiniburn); // netherrack and magma burn forever
    const int age = reg.intValue(state, "age");
    if (!forever && level.isRaining() && nearRain(level, pos) && r.nextFloat() < 0.2f + float(age) * 0.03f) {
        removeFire(level, pos);
        return;
    }
    const int aged = std::min(15, age + r.nextInt(3) / 2);
    if (aged != age) {
        state = withAge(reg, state, aged);
        level.setBlock(pos, state);
    }
    if (!forever) {
        bool fuel = false;
        for (Direction d : kDirections) fuel |= canBurn(reg, level.getBlock(pos.offset(d)));
        if (!fuel) {
            // Burning on a bare floor: it goes out once past age 3 (or with no floor).
            if (!level.sturdy(pos.below(), Direction::Up) || age > 3) removeFire(level, pos);
            return;
        }
        if (age == 15 && r.nextInt(4) == 0 && !canBurn(reg, below)) {
            removeFire(level, pos);
            return;
        }
    }
    // Biomes tagged increased_fire_burnout (wet jungles and swamps, snowy peaks) burn out sooner.
    bool burnout = false;
    if (level.biomes()) {
        const std::string_view biome = shortId(level.biomes()->get(level.biomeAt(pos)).id);
        for (std::string_view b : {"bamboo_jungle", "mushroom_fields", "mangrove_swamp", "snowy_slopes", "frozen_peaks",
                                   "jagged_peaks", "swamp", "jungle"})
            burnout |= biome == b;
    }
    const int k = burnout ? -50 : 0;
    checkBurnOut(level, pos.offset(Direction::East), 300 + k, age);
    checkBurnOut(level, pos.offset(Direction::West), 300 + k, age);
    checkBurnOut(level, pos.below(), 250 + k, age);
    checkBurnOut(level, pos.above(), 250 + k, age);
    checkBurnOut(level, pos.offset(Direction::North), 300 + k, age);
    checkBurnOut(level, pos.offset(Direction::South), 300 + k, age);
    // Spreading: to empty cells by their most flammable neighbor, less likely the higher.
    for (int x = -1; x <= 1; ++x)
        for (int z = -1; z <= 1; ++z)
            for (int y = -1; y <= 4; ++y) {
                if (x == 0 && y == 0 && z == 0) continue;
                const int odds = y > 1 ? 100 + (y - 1) * 100 : 100;
                const BlockPos p = pos.offset(x, y, z);
                const int ignite = igniteOddsAt(level, p);
                if (ignite <= 0) continue;
                int chance = (ignite + 40 + int(level.difficulty) * 7) / (age + 30);
                if (burnout) chance /= 2;
                if (chance > 0 && r.nextInt(odds) <= chance && (!level.isRaining() || !nearRain(level, p))) {
                    const int a = std::min(15, age + r.nextInt(5) / 4);
                    level.setBlockAndUpdate(p, fireWithAge(level, p, a));
                }
            }
}

void lavaRandomTick(Level& level, const BlockPos& pos) {
    if (!level.doFireTick) return;
    const BlockRegistry& reg = level.registry();
    Xoroshiro& r = level.random();
    // Vanilla isFlammable: blocks lava ignites (here: those fire spreads to).
    auto flammable = [&](const BlockPos& p) {
        return level.chunks().isReady(ChunkPos::of(p)) && reg.blockOf(level.getBlock(p)).igniteOdds > 0;
    };
    const int tries = r.nextInt(3);
    if (tries > 0) {
        // Up to two steps up and around: the first air cell beside fuel catches.
        BlockPos p = pos;
        for (int j = 0; j < tries; ++j) {
            const int dx = r.nextInt(3) - 1;
            const int dz = r.nextInt(3) - 1;
            p = p.offset(dx, 1, dz);
            if (!level.chunks().isReady(ChunkPos::of(p))) return;
            const StateId s = level.getBlock(p);
            if (reg.info(s).flags & kAir) {
                bool fuel = false;
                for (Direction d : kDirections) fuel |= flammable(p.offset(d));
                if (fuel) {
                    level.setBlockAndUpdate(p, fireStateAt(reg, level, p));
                    return;
                }
            } else if (reg.info(s).flags & kBlocksMotion) {
                return;
            }
        }
    } else {
        // Or the tops of flammable blocks beside it.
        for (int k = 0; k < 3; ++k) {
            const int dx = r.nextInt(3) - 1;
            const int dz = r.nextInt(3) - 1;
            const BlockPos p = pos.offset(dx, 0, dz);
            if (!level.chunks().isReady(ChunkPos::of(p))) return;
            if ((reg.info(level.getBlock(p.above())).flags & kAir) && flammable(p))
                level.setBlockAndUpdate(p.above(), fireStateAt(reg, level, p.above()));
        }
    }
}

bool ignite(Level& level, const BlockPos& pos) {
    const BlockRegistry& reg = level.registry();
    if (!(reg.info(level.getBlock(pos)).flags & kAir)) return false;
    const StateId fire = fireStateAt(reg, level, pos);
    if (!fireCanSurvive(reg, level, fire, pos)) return false;
    return level.setBlockAndUpdate(pos, fire);
}

} // namespace mc::sim
