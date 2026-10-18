#include "entity/DisplayTicks.h"

#include <array>

#include "entity/Particles.h"
#include "resources/ResourcePack.h"
#include "world/level/Level.h"

namespace mc {
namespace {

using P = ParticleType;

glm::vec3 rgb(uint32_t c) { return glm::vec3(float((c >> 16) & 255), float((c >> 8) & 255), float(c & 255)) / 255.0f; }

// Vanilla ClientLevel.trySpawnDripParticles: a drip under the fluid's floor block.
void dripUnder(const Level& level, const BlockPos& pos, ParticleType drip, bool fluidFloorSturdy, ParticleSystem& out) {
    const BlockRegistry& reg = level.registry();
    Xoroshiro& r = out.random();
    const StateId s = level.getBlock(pos);
    if (!reg.info(s).fluid.empty()) return;
    const Shape& shape = reg.collisionShape(s);
    double top = 0.0, bottom = 1.0;
    glm::dvec2 lo(0.0), hi(1.0);
    if (!shape.empty()) {
        lo = glm::dvec2(1.0);
        hi = glm::dvec2(0.0);
        for (const AABB& b : shape) {
            top = std::max(top, b.max.y);
            bottom = std::min(bottom, b.min.y);
            lo = glm::min(lo, glm::dvec2(b.min.x, b.min.z));
            hi = glm::max(hi, glm::dvec2(b.max.x, b.max.z));
        }
    }
    auto spawn = [&](double x0, double x1, double z0, double z1, double y) {
        const double a = r.nextDouble(), b = r.nextDouble();
        out.add(drip, {x0 + (x1 - x0) * a, y, z0 + (z1 - z0) * b});
    };
    if (top < 1.0) {
        if (fluidFloorSturdy) spawn(pos.x, pos.x + 1.0, pos.z, pos.z + 1.0, pos.y + 1.0 - 0.05);
        return;
    }
    // Full-height blocks let it through unless impermeable (glass).
    const std::string_view id = shortId(reg.blockOf(s).id);
    if (id.ends_with("glass") || reg.blockOf(s).className == "pane") return;
    if (bottom > 0.0) {
        spawn(pos.x + lo.x, pos.x + hi.x, pos.z + lo.y, pos.z + hi.y, pos.y + bottom - 0.05);
        return;
    }
    const StateId under = level.getBlock(pos.below());
    double underTop = 0.0;
    for (const AABB& b : reg.collisionShape(under)) underTop = std::max(underTop, b.max.y);
    if (underTop < 1.0 && reg.info(under).fluid.empty())
        spawn(pos.x + lo.x, pos.x + hi.x, pos.z + lo.y, pos.z + hi.y, pos.y - 0.05);
}

// Vanilla AbstractCandleBlock: where the flames sit for 1-4 candles.
const std::array<std::array<glm::dvec3, 4>, 4> kCandleFlames = {{
    {{{0.5, 0.5, 0.5}}},
    {{{0.375, 0.44, 0.5}, {0.625, 0.5, 0.44}}},
    {{{0.5, 0.313, 0.625}, {0.375, 0.44, 0.5}, {0.56, 0.5, 0.44}}},
    {{{0.44, 0.313, 0.56}, {0.625, 0.44, 0.56}, {0.375, 0.44, 0.375}, {0.56, 0.5, 0.375}}},
}};

void candleFlame(const glm::dvec3& at, ParticleSystem& out) {
    const float f = out.random().nextFloat();
    if (f < 0.3f) out.add(P::Smoke, at);
    out.add(P::SmallFlame, at);
}

// One block's (and its fluid's) animateTick.
void blockDisplayTick(const Level& level, const BlockPos& pos, ParticleSystem& out, const TintAt& tint) {
    const BlockRegistry& reg = level.registry();
    Xoroshiro& r = out.random();
    const StateId s = level.getBlock(pos);
    const StateInfo& info = reg.info(s);
    const Block& b = reg.blockOf(s);
    const std::string_view cls = b.className, id = shortId(b.id);
    const glm::dvec3 o(pos.x, pos.y, pos.z);
    if (cls == "torch" || cls == "wall_torch") {
        const ParticleType flame = id.starts_with("soul") ? P::SoulFireFlame : P::Flame;
        glm::dvec3 at = o + glm::dvec3(0.5, 0.7, 0.5);
        if (cls == "wall_torch") {
            const glm::ivec3 back = normal(opposite(*directionFromName(reg.value(s, "facing"))));
            at += glm::dvec3(0.27 * back.x, 0.22, 0.27 * back.z);
        }
        out.add(P::Smoke, at);
        out.add(flame, at);
    } else if (cls == "fire" || id == "soul_fire") {
        // Large smoke rising off it, or off the sides it clings to.
        const StateId below = level.getBlock(pos.below());
        const bool burnable = reg.blockOf(below).igniteOdds > 0;
        if (!burnable && !level.sturdy(pos.below(), Direction::Up)) {
            auto burns = [&](Direction d) { return reg.blockOf(level.getBlock(pos.offset(d))).igniteOdds > 0; };
            for (Direction d : {Direction::West, Direction::East, Direction::North, Direction::South, Direction::Up}) {
                if (!burns(d)) continue;
                for (int j = 0; j < 2; ++j) {
                    glm::dvec3 at(r.nextDouble(), r.nextDouble(), r.nextDouble());
                    const double edge = r.nextDouble() * double(0.1f);
                    if (d == Direction::West) at.x = edge;
                    if (d == Direction::East) at.x = 1.0 - edge;
                    if (d == Direction::North) at.z = edge;
                    if (d == Direction::South) at.z = 1.0 - edge;
                    if (d == Direction::Up) at.y = 1.0 - edge;
                    out.add(P::LargeSmoke, o + at);
                }
            }
        } else {
            for (int i = 0; i < 3; ++i) {
                const double x = r.nextDouble(), y = r.nextDouble() * 0.5 + 0.5, z = r.nextDouble();
                out.add(P::LargeSmoke, o + glm::dvec3(x, y, z));
            }
        }
    } else if (cls == "furnace" && reg.boolValue(s, "lit")) {
        const glm::ivec3 n = normal(*directionFromName(reg.value(s, "facing")));
        const double h = r.nextDouble() * 0.6 - 0.3;
        const double dx = n.x != 0 ? n.x * 0.52 : h;
        const double dy = r.nextDouble() * 6.0 / 16.0;
        const double dz = n.z != 0 ? n.z * 0.52 : h;
        const glm::dvec3 at = o + glm::dvec3(0.5 + dx, dy, 0.5 + dz);
        if (id == "smoker") {
            out.add(P::Smoke, o + glm::dvec3(0.5, 1.1, 0.5));
        } else {
            out.add(P::Smoke, at);
            if (id == "furnace") out.add(P::Flame, at);
        }
    } else if ((cls == "candle" || cls == "candle_cake") && reg.boolValue(s, "lit")) {
        if (cls == "candle_cake") {
            candleFlame(o + glm::dvec3(0.5, 1.0, 0.5), out);
        } else {
            const int n = std::clamp(reg.intValue(s, "candles", 1), 1, 4);
            for (int i = 0; i < n; ++i) candleFlame(o + kCandleFlames[size_t(n - 1)][size_t(i)], out);
        }
    } else if (cls == "leaves") {
        // Rain drips through; leaves fall where nothing solid is under them (vanilla 1.21.5).
        const BlockPos under = pos.below();
        const StateId below = level.getBlock(under);
        if (level.isRainingAt(pos.above()) && r.nextInt(15) == 1 &&
            (!(reg.info(below).flags & kOpaqueCube) || !level.sturdy(under, Direction::Up))) {
            const double x = r.nextDouble(), z = r.nextDouble();
            out.add(P::DrippingWater, o + glm::dvec3(x, -0.05, z));
        }
        const bool cherry = id == "cherry_leaves";
        if (id == "pale_oak_leaves" || r.nextFloat() >= (cherry ? 0.1f : 0.01f)) return;
        if (!(reg.info(below).sturdyFaces & (1u << int(Direction::Up)))) {
            const double x = r.nextDouble(), z = r.nextDouble();
            const glm::dvec3 at = o + glm::dvec3(x, -0.05, z);
            if (cherry) out.add(P::CherryLeaves, at);
            else if (id == "azalea_leaves" || id == "flowering_azalea_leaves") out.add(P::TintedLeaves, at, {}, rgb(0x70922D));
            else out.add(P::TintedLeaves, at, {}, rgb(tint(pos, s)));
        }
    } else if (id == "wet_sponge") {
        const Direction d = kDirections[size_t(r.nextInt(6))];
        if (d != Direction::Up && !level.sturdy(pos.offset(d), opposite(d))) {
            glm::dvec3 at = o;
            if (d == Direction::Down) {
                at.y -= 0.05;
                at.x += r.nextDouble();
                at.z += r.nextDouble();
            } else {
                at.y += r.nextDouble() * 0.8;
                if (axisOf(d) == Axis::X) {
                    at.z += r.nextDouble();
                    at.x += d == Direction::East ? 1.0 : 0.05;
                } else {
                    at.x += r.nextDouble();
                    at.z += d == Direction::South ? 1.0 : 0.05;
                }
            }
            out.add(P::DrippingWater, at);
        }
    }
    // The fluid: motes in water, pops on open lava, and drips under both.
    const FluidState& f = info.fluid;
    if (f.empty()) return;
    if (f.kind == FluidKind::Water) {
        if ((f.source() || f.falling) && r.nextInt(10) == 0) {
            const double x = r.nextDouble(), y = r.nextDouble(), z = r.nextDouble();
            out.add(P::Underwater, o + glm::dvec3(x, y, z));
        }
    } else {
        const StateId above = level.getBlock(pos.above());
        if ((reg.info(above).flags & kAir) && r.nextInt(100) == 0) {
            const double x = r.nextDouble(), z = r.nextDouble();
            out.add(P::Lava, o + glm::dvec3(x, 1.0, z));
        }
    }
    if (r.nextInt(10) == 0)
        dripUnder(level, pos.below(), f.kind == FluidKind::Water ? P::DrippingWater : P::DrippingLava,
                  level.sturdy(pos, Direction::Down), out);
}

} // namespace

void animateTick(const Level& level, const BlockPos& center, ParticleSystem& particles, const TintAt& tint) {
    Xoroshiro& r = particles.random();
    for (int j = 0; j < 667; ++j)
        for (const int range : {16, 32}) {
            const int a = r.nextInt(range), b = r.nextInt(range), c = r.nextInt(range), d = r.nextInt(range),
                      e = r.nextInt(range), f = r.nextInt(range);
            const BlockPos p = center.offset(a - b, c - d, e - f);
            if (p.y < kWorldMinY || p.y > kWorldMaxY || !level.chunks().isReady(ChunkPos::of(p))) continue;
            blockDisplayTick(level, p, particles, tint);
        }
}

void tickRainParticles(const Level& level, const BlockPos& center, int64_t ticks, ParticleSystem& particles) {
    const float rain = level.rainLevel(1.0f);
    if (rain <= 0.0f) return;
    const BlockRegistry& reg = level.registry();
    Xoroshiro r(ticks * 312987231LL);
    const int count = int(100.0f * rain * rain);
    for (int j = 0; j < count; ++j) {
        const int dx = r.nextInt(21) - 10;
        const int dz = r.nextInt(21) - 10;
        const int x = center.x + dx, z = center.z + dz;
        const int top = level.motionBlockingHeight(x, z);
        if (top <= kWorldMinY || top > center.y + 10 || top < center.y - 10) continue;
        const BlockPos ground(x, top - 1, z);
        if (level.precipitationAt(BlockPos(x, top, z)) != Level::Precipitation::Rain) continue;
        // On the block's top (or its fluid's surface); smoke where it lands on lava or magma.
        const double u = r.nextDouble(), v = r.nextDouble();
        const StateId s = level.getBlock(ground);
        double height = 0.0;
        for (const AABB& b : reg.collisionShape(s))
            if (u >= b.min.x && u <= b.max.x && v >= b.min.z && v <= b.max.z) height = std::max(height, b.max.y);
        const FluidState& f = reg.info(s).fluid;
        if (!f.empty()) height = std::max(height, f.source() || f.falling ? 8.0 / 9.0 : double(8 - f.level) / 9.0);
        const std::string_view id = shortId(reg.blockOf(s).id);
        const bool hot = f.kind == FluidKind::Lava || id == "magma_block" ||
                         ((id == "campfire" || id == "soul_campfire") && reg.boolValue(s, "lit"));
        particles.add(hot ? P::Smoke : P::Rain, {ground.x + u, ground.y + height, ground.z + v});
    }
}

} // namespace mc
