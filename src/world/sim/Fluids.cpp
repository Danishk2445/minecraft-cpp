#include "world/sim/Fluids.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <unordered_map>
#include <vector>

#include "resources/ResourcePack.h"
#include "world/level/Level.h"
#include "world/sim/Simulation.h"

namespace mc::sim {
namespace {

// Vanilla Direction.Plane.HORIZONTAL order.
constexpr std::array<Direction, 4> kPlane = {Direction::North, Direction::East, Direction::South, Direction::West};

bool sameFluid(const Fluid& a, const Fluid& b) {
    return a.kind == b.kind && a.amount == b.amount && a.falling == b.falling && a.source == b.source;
}

// Vanilla Shapes.mergedFaceOccludes: the faces of `self` toward `dir` and of `other` toward
// it together cover the whole side (on a 16 x 16 raster of the face).
bool facesCover(const Shape& self, const Shape& other, Direction dir) {
    const int axis = int(axisOf(dir));
    const int u = axis == 0 ? 1 : 0, v = axis == 2 ? 1 : 2;
    const bool positive = normal(dir)[axis] > 0;
    std::bitset<256> covered;
    auto add = [&](const Shape& shape, bool atMax) {
        constexpr double eps = 1e-7;
        for (const AABB& raw : shape) {
            const glm::dvec3 lo = glm::clamp(raw.min, 0.0, 1.0), hi = glm::clamp(raw.max, 0.0, 1.0);
            if (atMax ? hi[axis] < 1.0 - eps : lo[axis] > eps) continue; // not on that side
            const int u0 = int(std::ceil(lo[u] * 16.0 - eps)), u1 = int(std::floor(hi[u] * 16.0 + eps));
            const int v0 = int(std::ceil(lo[v] * 16.0 - eps)), v1 = int(std::floor(hi[v] * 16.0 + eps));
            for (int a = u0; a < u1; ++a)
                for (int b = v0; b < v1; ++b) covered.set(size_t(a * 16 + b));
        }
    };
    add(self, positive);
    add(other, !positive);
    return covered.all();
}

// Per-block facts the fluid code asks for constantly, cached per registry.
struct Tables {
    enum : uint8_t { kWaterloggable = 1, kWaterPlant = 2, kNoFluid = 4 };
    std::vector<uint8_t> flags; // per block
    std::array<StateId, 16> water{}, lava{};
    StateId stone = 0, obsidian = 0, cobblestone = 0, basalt = 0;
    uint16_t soulSoil = 0xFFFF, blueIce = 0xFFFF, ice = 0xFFFF, packedIce = 0xFFFF, frostedIce = 0xFFFF;

    explicit Tables(const BlockRegistry& reg) {
        flags.resize(reg.blockCount());
        for (size_t i = 0; i < reg.blockCount(); ++i) {
            const Block& b = reg.block(i);
            const std::string_view id = shortId(b.id);
            uint8_t f = 0;
            if (!reg.value(b.defaultState, "waterlogged").empty()) f |= kWaterloggable;
            if (b.className == "water_plant" || b.className == "tall_water_plant" || b.className == "kelp" ||
                id == "kelp_plant")
                f |= kWaterPlant;
            // Vanilla canHoldAnyFluid exceptions (ladders and signs are waterloggable anyway).
            if (b.className == "door" || id == "sugar_cane" || id == "bubble_column" || id == "nether_portal" ||
                id == "end_portal" || id == "end_gateway" || id == "structure_void")
                f |= kNoFluid;
            flags[i] = f;
        }
        for (int lvl = 0; lvl < 16; ++lvl) {
            water[size_t(lvl)] = reg.with(reg.defaultState("water"), "level", std::to_string(lvl)).value_or(0);
            lava[size_t(lvl)] = reg.with(reg.defaultState("lava"), "level", std::to_string(lvl)).value_or(0);
        }
        stone = reg.defaultState("stone");
        obsidian = reg.defaultState("obsidian");
        cobblestone = reg.defaultState("cobblestone");
        basalt = reg.defaultState("basalt");
        auto index = [&](const char* id) -> uint16_t {
            const Block* b = reg.find(id);
            return b ? b->index : uint16_t(0xFFFF);
        };
        soulSoil = index("soul_soil");
        blueIce = index("blue_ice");
        ice = index("ice");
        packedIce = index("packed_ice");
        frostedIce = index("frosted_ice");
    }
};

const Tables& tables(const BlockRegistry& reg) {
    static thread_local const BlockRegistry* owner = nullptr;
    static thread_local std::optional<Tables> t;
    if (owner != &reg) {
        t.emplace(reg);
        owner = &reg;
    }
    return *t;
}

uint8_t flagsOf(const BlockRegistry& reg, StateId s) { return tables(reg).flags[reg.info(s).block]; }

bool canConvertToSource(const Level& level, FluidKind kind) {
    return kind == FluidKind::Water ? level.waterSourceConversion : level.lavaSourceConversion;
}

// Vanilla canPassThroughWall: no full cube on either side, and the two blocks' collision
// faces between them leave an opening.
bool canPassThroughWall(const BlockRegistry& reg, Direction dir, StateId state, StateId spreadState) {
    const uint16_t into = reg.info(spreadState).collision, from = reg.info(state).collision;
    if (into == 1 || from == 1) return false;
    if (into == 0 && from == 0) return true;
    return !facesCover(reg.shape(from), reg.shape(into), dir);
}

// Vanilla canHoldAnyFluid / canHoldSpecificFluid. `kind` with `source`: the fluid that
// would go in (waterloggable blocks take only a water source).
bool canHoldAnyFluid(const BlockRegistry& reg, StateId s) {
    const uint8_t f = flagsOf(reg, s);
    if (f & (Tables::kWaterloggable | Tables::kWaterPlant)) return true;
    if (reg.info(s).flags & kBlocksMotion) return false;
    return !(f & Tables::kNoFluid);
}
bool canHoldSpecificFluid(const BlockRegistry& reg, StateId s, FluidKind kind, bool source) {
    const uint8_t f = flagsOf(reg, s);
    if (f & Tables::kWaterPlant) return false;
    if (f & Tables::kWaterloggable) return kind == FluidKind::Water && source;
    return true;
}

// Vanilla canBeReplacedWith of the fluid already in a cell: water only gives way to
// another fluid from above, lava (4/9 or deeper) to water.
bool canBeReplaced(const Fluid& existing, FluidKind incoming, Direction dir) {
    if (existing.empty()) return true;
    if (existing.kind == FluidKind::Water) return dir == Direction::Down && incoming != FluidKind::Water;
    return existing.height() >= 0.44444445f && incoming == FluidKind::Water;
}

bool canMaybePassThrough(const BlockRegistry& reg, FluidKind kind, Direction dir, StateId state, StateId spreadState,
                         const Fluid& spreadFluid) {
    const bool sourceOfThis = spreadFluid.kind == kind && spreadFluid.source;
    return !sourceOfThis && canHoldAnyFluid(reg, spreadState) && canPassThroughWall(reg, dir, state, spreadState);
}

// Vanilla getNewLiquid: what the cell `pos` (holding `state`) would hold from its neighbors.
Fluid newLiquid(const Level& level, const BlockPos& pos, StateId state, FluidKind kind) {
    const BlockRegistry& reg = level.registry();
    int maxAmount = 0, sources = 0;
    for (Direction d : kPlane) {
        const StateId ns = level.getBlock(pos.offset(d));
        const Fluid nf = fluidOf(reg, ns);
        if (nf.kind == kind && canPassThroughWall(reg, d, state, ns)) {
            if (nf.source) ++sources;
            maxAmount = std::max(maxAmount, nf.amount);
        }
    }
    if (sources >= 2 && canConvertToSource(level, kind)) {
        const StateId below = level.getBlock(pos.below());
        const Fluid bf = fluidOf(reg, below);
        if ((reg.info(below).flags & kBlocksMotion) || (bf.kind == kind && bf.source)) return {kind, 8, false, true};
    }
    const StateId above = level.getBlock(pos.above());
    if (fluidOf(reg, above).kind == kind && canPassThroughWall(reg, Direction::Up, state, above))
        return {kind, 8, true, false};
    const int k = maxAmount - fluidDropOff(kind);
    if (k <= 0) return {};
    return {kind, k, false, false};
}

// Vanilla isWaterHole: the fluid at `pos` could fall into the cell below.
bool isHole(const BlockRegistry& reg, FluidKind kind, StateId state, StateId below) {
    if (!canPassThroughWall(reg, Direction::Down, state, below)) return false;
    if (fluidOf(reg, below).kind == kind) return true;
    return canHoldAnyFluid(reg, below) && canHoldSpecificFluid(reg, below, kind, false);
}

// Vanilla SpreadContext: block states and holes cached for one spread.
struct SpreadContext {
    const Level& level;
    FluidKind kind;
    std::unordered_map<BlockPos, StateId> states;
    std::unordered_map<BlockPos, bool> holes;
    StateId state(const BlockPos& p) {
        auto [it, fresh] = states.try_emplace(p, 0);
        if (fresh) it->second = level.getBlock(p);
        return it->second;
    }
    bool hole(const BlockPos& p) {
        if (auto it = holes.find(p); it != holes.end()) return it->second;
        const bool h = isHole(level.registry(), kind, state(p), state(p.below()));
        holes.emplace(p, h);
        return h;
    }
};

// Vanilla getSlopeDistance: steps to the nearest hole within the fluid's search distance.
int slopeDistance(SpreadContext& ctx, const BlockPos& pos, int depth, Direction from, StateId state) {
    const BlockRegistry& reg = ctx.level.registry();
    int best = 1000;
    for (Direction d : kPlane) {
        if (d == from) continue;
        const BlockPos n = pos.offset(d);
        const StateId ns = ctx.state(n);
        if (!canMaybePassThrough(reg, ctx.kind, d, state, ns, fluidOf(reg, ns)) ||
            !canHoldSpecificFluid(reg, ns, ctx.kind, false))
            continue;
        if (ctx.hole(n)) return depth;
        if (depth < fluidSlopeFind(ctx.kind)) best = std::min(best, slopeDistance(ctx, n, depth + 1, opposite(d), ns));
    }
    return best;
}

// Vanilla getSpread: the sides toward the nearest way down (all of them on flat ground),
// with what each would hold; in Direction order (north, south, west, east).
std::vector<std::pair<Direction, Fluid>> getSpread(Level& level, const BlockPos& pos, StateId state, FluidKind kind) {
    const BlockRegistry& reg = level.registry();
    int best = 1000;
    std::vector<std::pair<Direction, Fluid>> out;
    SpreadContext ctx{level, kind, {}, {}};
    for (Direction d : kPlane) {
        const BlockPos n = pos.offset(d);
        const StateId ns = level.getBlock(n);
        const Fluid nf = fluidOf(reg, ns);
        if (!canMaybePassThrough(reg, kind, d, state, ns, nf)) continue;
        const Fluid nl = newLiquid(level, n, ns, kind);
        if (nl.empty() || !canHoldSpecificFluid(reg, ns, nl.kind, nl.source)) continue;
        const int slope = ctx.hole(n) ? 0 : slopeDistance(ctx, n, 1, opposite(d), ns);
        if (slope < best) out.clear();
        if (slope <= best) {
            if (canBeReplaced(nf, nl.kind, d)) out.emplace_back(d, nl);
            best = slope;
        }
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return int(a.first) < int(b.first); });
    return out;
}

// Vanilla spreadTo: the fluid flows into `pos`, washing away (water: with its drops) or
// burning (lava) what stood there; lava falling into water makes stone.
void spreadTo(Level& level, const BlockPos& pos, StateId state, Direction dir, const Fluid& f) {
    const BlockRegistry& reg = level.registry();
    const Tables& t = tables(reg);
    if (dir == Direction::Down && f.kind == FluidKind::Lava && fluidOf(reg, state).kind == FluidKind::Water) {
        if (reg.blockOf(state).className == "liquid") level.setBlockAndUpdate(pos, t.stone);
        level.levelEvent(LevelEventType::LavaFizz, pos);
        return;
    }
    const uint8_t flags = flagsOf(reg, state);
    if (flags & Tables::kWaterloggable) {
        // LiquidBlockContainer.placeLiquid: a water source fills it.
        if (!reg.boolValue(state, "waterlogged") && f.kind == FluidKind::Water && f.source) {
            level.setBlockAndUpdate(pos, *reg.with(state, "waterlogged", "true"));
            level.scheduleFluidTick(pos, fluidTickType(FluidKind::Water, true), fluidTickDelay(FluidKind::Water));
        }
        return;
    }
    if (flags & Tables::kWaterPlant) return;
    if (!(reg.info(state).flags & kAir)) {
        if (f.kind == FluidKind::Water) level.dropResources(pos, state);
        else level.levelEvent(LevelEventType::LavaFizz, pos);
    }
    level.setBlockAndUpdate(pos, liquidBlock(reg, f.kind, f.source ? 8 : f.amount, f.falling && !f.source));
}

int sourceNeighborCount(const Level& level, const BlockPos& pos, FluidKind kind) {
    int n = 0;
    for (Direction d : kPlane) {
        const Fluid f = fluidOf(level.registry(), level.getBlock(pos.offset(d)));
        if (f.kind == kind && f.source) ++n;
    }
    return n;
}

void spreadToSides(Level& level, const BlockPos& pos, const Fluid& f, StateId state) {
    const int amount = f.falling ? 7 : f.amount - fluidDropOff(f.kind);
    if (amount <= 0) return;
    for (const auto& [dir, next] : getSpread(level, pos, state, f.kind)) {
        const BlockPos n = pos.offset(dir);
        spreadTo(level, n, level.getBlock(n), dir, next);
    }
}

// Vanilla FlowingFluid.spread: down first (and to the sides as well from a cell between
// three sources), else to the sides unless it is about to fall.
void spread(Level& level, const BlockPos& pos, StateId state, const Fluid& f) {
    if (f.empty()) return;
    const BlockRegistry& reg = level.registry();
    const BlockPos below = pos.below();
    const StateId bs = level.getBlock(below);
    const Fluid bf = fluidOf(reg, bs);
    if (canMaybePassThrough(reg, f.kind, Direction::Down, state, bs, bf)) {
        const Fluid next = newLiquid(level, below, bs, f.kind);
        if (!next.empty() && canBeReplaced(bf, next.kind, Direction::Down) &&
            canHoldSpecificFluid(reg, bs, next.kind, next.source)) {
            spreadTo(level, below, bs, Direction::Down, next);
            if (sourceNeighborCount(level, pos, f.kind) >= 3) spreadToSides(level, pos, f, state);
            return;
        }
    }
    if (f.source || !isHole(reg, f.kind, state, bs)) spreadToSides(level, pos, f, state);
}

// Lava sometimes waits four times as long before rising (vanilla LavaFluid.getSpreadDelay).
int spreadDelay(Level& level, const Fluid& current, const Fluid& next) {
    int delay = fluidTickDelay(current.kind);
    if (current.kind == FluidKind::Lava && !next.empty() && !current.falling && !next.falling &&
        next.height() > current.height() && level.random().nextInt(4) != 0)
        delay *= 4;
    return delay;
}

} // namespace

Fluid fluidOf(const BlockRegistry& registry, StateId state) {
    const FluidState& f = registry.info(state).fluid;
    if (f.empty()) return {};
    if (f.source()) return {f.kind, 8, false, true};
    if (f.falling) return {f.kind, 8, true, false};
    return {f.kind, 8 - f.level, false, false};
}

int fluidDropOff(FluidKind kind) { return kind == FluidKind::Lava ? 2 : 1; }
int fluidSlopeFind(FluidKind kind) { return kind == FluidKind::Lava ? 2 : 4; }
int fluidTickDelay(FluidKind kind) { return kind == FluidKind::Lava ? 30 : 5; }

StateId liquidBlock(const BlockRegistry& registry, FluidKind kind, int amount, bool falling) {
    const Tables& t = tables(registry);
    const int lvl = falling ? 8 : amount >= 8 ? 0 : 8 - std::clamp(amount, 1, 7);
    return kind == FluidKind::Lava ? t.lava[size_t(lvl)] : t.water[size_t(lvl)];
}

void fluidTick(Level& level, const BlockPos& pos, StateId state) {
    const BlockRegistry& reg = level.registry();
    Fluid f = fluidOf(reg, state);
    if (f.empty()) return;
    if (!f.source) {
        const Fluid next = newLiquid(level, pos, state, f.kind);
        const int delay = spreadDelay(level, f, next);
        if (next.empty()) {
            f = next;
            state = BlockRegistry::kAirState;
            level.setBlockAndUpdate(pos, state);
        } else if (!sameFluid(next, f)) {
            f = next;
            state = liquidBlock(reg, f.kind, f.source ? 8 : f.amount, f.falling);
            level.setBlockAndUpdate(pos, state);
            level.scheduleFluidTick(pos, fluidTickType(f.kind, f.source), delay);
        }
    }
    spread(level, pos, state, f);
}

bool lavaHardens(Level& level, const BlockPos& pos, StateId state) {
    const BlockRegistry& reg = level.registry();
    const Fluid f = fluidOf(reg, state);
    if (f.kind != FluidKind::Lava) return false;
    const Tables& t = tables(reg);
    const bool onSoulSoil = reg.info(level.getBlock(pos.below())).block == t.soulSoil;
    // Vanilla POSSIBLE_FLOW_DIRECTIONS (down and the sides), reversed: above and the sides.
    for (Direction d : {Direction::Up, Direction::North, Direction::South, Direction::West, Direction::East}) {
        const StateId ns = level.getBlock(pos.offset(d));
        if (fluidOf(reg, ns).kind == FluidKind::Water) {
            level.setBlockAndUpdate(pos, f.source ? t.obsidian : t.cobblestone);
            level.levelEvent(LevelEventType::LavaFizz, pos);
            return true;
        }
        if (onSoulSoil && reg.info(ns).block == t.blueIce) {
            level.setBlockAndUpdate(pos, t.basalt);
            level.levelEvent(LevelEventType::LavaFizz, pos);
            return true;
        }
    }
    return false;
}

bool canFlowOut(const Level& level, const BlockPos& pos, StateId state, Direction dir) {
    const BlockRegistry& reg = level.registry();
    const Fluid f = fluidOf(reg, state);
    if (f.empty() || (dir == Direction::Up)) return false;
    const StateId ns = level.getBlock(pos.offset(dir));
    const Fluid nf = fluidOf(reg, ns);
    if (nf.kind == f.kind) return false;
    return canMaybePassThrough(reg, f.kind, dir, state, ns, nf) && canHoldSpecificFluid(reg, ns, f.kind, false) &&
           canBeReplaced(nf, f.kind, dir);
}

glm::dvec3 fluidFlow(const BlockGetter& level, const BlockPos& pos) {
    const BlockRegistry& reg = level.registry();
    const Fluid f = fluidOf(reg, level.getBlock(pos));
    if (f.empty()) return glm::dvec3(0.0);
    auto affects = [&](const Fluid& o) { return o.empty() || o.kind == f.kind; };
    double dx = 0.0, dz = 0.0;
    for (Direction d : kPlane) {
        const BlockPos n = pos.offset(d);
        const StateId ns = level.getBlock(n);
        const Fluid nf = fluidOf(reg, ns);
        if (!affects(nf)) continue;
        float h = nf.empty() ? 0.0f : nf.height();
        float g = 0.0f;
        if (h == 0.0f) {
            if (!(reg.info(ns).flags & kBlocksMotion)) {
                const Fluid bf = fluidOf(reg, level.getBlock(n.below()));
                if (affects(bf)) {
                    h = bf.empty() ? 0.0f : bf.height();
                    if (h > 0.0f) g = f.height() - (h - 0.8888889f);
                }
            }
        } else if (h > 0.0f) {
            g = f.height() - h;
        }
        if (g != 0.0f) {
            dx += double(normal(d).x) * double(g);
            dz += double(normal(d).z) * double(g);
        }
    }
    auto normalized = [](glm::dvec3 v) {
        const double len = glm::length(v);
        return len < 1.0e-4 ? glm::dvec3(0.0) : v / len;
    };
    glm::dvec3 flow(dx, 0.0, dz);
    if (f.falling) {
        // Vanilla isSolidFace: a sturdy side of a neighbor (or the one above it), not ice.
        auto solidFace = [&](const BlockPos& p, Direction side) {
            const StateId s = level.getBlock(p);
            if (fluidOf(reg, s).kind == f.kind) return false;
            if (side == Direction::Up) return true;
            const uint16_t b = reg.info(s).block;
            const Tables& t = tables(reg);
            if (b == t.ice || b == t.packedIce || b == t.frostedIce || b == t.blueIce) return false;
            return (reg.info(s).sturdyFaces & (1u << int(side))) != 0;
        };
        for (Direction d : kPlane) {
            const BlockPos n = pos.offset(d);
            if (solidFace(n, d) || solidFace(n.above(), d)) {
                flow = normalized(flow) + glm::dvec3(0.0, -6.0, 0.0);
                break;
            }
        }
    }
    return normalized(flow);
}

} // namespace mc::sim
