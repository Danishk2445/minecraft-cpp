#include "world/sim/Explosion.h"

#include <cmath>
#include <unordered_set>
#include <vector>

#include "entity/ItemEntity.h"
#include "entity/LivingEntity.h"
#include "entity/Player.h"
#include "entity/PrimedTnt.h"
#include "game/BlockEntities.h"
#include "game/Loot.h"
#include "resources/ResourcePack.h"
#include "world/level/Level.h"
#include "world/level/Raycast.h"

namespace mc::sim {
namespace {

// The blocks the 1352 rays from the surface of a 16x16x16 grid of directions break.
std::vector<BlockPos> explodedBlocks(Level& level, const glm::dvec3& center, float power) {
    const BlockRegistry& reg = level.registry();
    Xoroshiro& r = level.random();
    std::unordered_set<BlockPos> seen;
    std::vector<BlockPos> out;
    for (int j = 0; j < 16; ++j)
        for (int k = 0; k < 16; ++k)
            for (int l = 0; l < 16; ++l) {
                if (j != 0 && j != 15 && k != 0 && k != 15 && l != 0 && l != 15) continue;
                glm::dvec3 d(double(float(j) / 15.0f * 2.0f - 1.0f), double(float(k) / 15.0f * 2.0f - 1.0f),
                             double(float(l) / 15.0f * 2.0f - 1.0f));
                d /= std::sqrt(glm::dot(d, d));
                float strength = power * (0.7f + r.nextFloat() * 0.6f);
                glm::dvec3 p = center;
                for (; strength > 0.0f; strength -= 0.22500001f) {
                    const BlockPos b = BlockPos::containing(p);
                    if (b.y < kWorldMinY || b.y > kWorldMaxY) break;
                    const StateId s = level.getBlock(b);
                    const StateInfo& info = reg.info(s);
                    const bool empty = (info.flags & kAir) && info.fluid.empty();
                    if (!empty) {
                        // The block's resistance, or its fluid's (water and lava 100).
                        float resistance = reg.blockOf(s).resistance;
                        if (!info.fluid.empty()) resistance = std::max(resistance, 100.0f);
                        strength -= (resistance + 0.3f) * 0.3f;
                    }
                    if (strength > 0.0f && !empty && seen.insert(b).second) out.push_back(b);
                    p += d * double(0.3f);
                }
            }
    return out;
}

// Vanilla getEntityDamageAmount: up to 7 x 2 x power + 1 at the center.
float damageAt(float power, double distance01, float seen) {
    const double e = (1.0 - distance01) * double(seen);
    return float((e * e + e) / 2.0 * 7.0 * double(power * 2.0f) + 1.0);
}

} // namespace

float seenPercent(const BlockGetter& level, const glm::dvec3& center, const AABB& box) {
    const glm::dvec3 size = box.max - box.min;
    const double d = 1.0 / (size.x * 2.0 + 1.0), e = 1.0 / (size.y * 2.0 + 1.0), f = 1.0 / (size.z * 2.0 + 1.0);
    const double g = (1.0 - std::floor(1.0 / d) * d) / 2.0, h = (1.0 - std::floor(1.0 / f) * f) / 2.0;
    int clear = 0, total = 0;
    for (double k = 0.0; k <= 1.0; k += d)
        for (double l = 0.0; l <= 1.0; l += e)
            for (double m = 0.0; m <= 1.0; m += f) {
                const glm::dvec3 from(box.min.x + (box.max.x - box.min.x) * k + g, box.min.y + (box.max.y - box.min.y) * l,
                                      box.min.z + (box.max.z - box.min.z) * m + h);
                const glm::dvec3 ray = center - from;
                const double length = std::sqrt(glm::dot(ray, ray));
                if (length < 1e-9 || !raycastBlocks(level, from, ray / length, length, RayFluid::None, RayShape::Collider))
                    ++clear;
                ++total;
            }
    return total ? float(clear) / float(total) : 0.0f;
}

void explode(Level& level, const Entity* source, const glm::dvec3& center, float power, bool breakBlocks, bool decay) {
    const BlockRegistry& reg = level.registry();
    std::vector<BlockPos> blocks = breakBlocks ? explodedBlocks(level, center, power) : std::vector<BlockPos>{};
    const auto* living = dynamic_cast<const LivingEntity*>(source);
    DamageSource damage = DamageSource::explosion(living);
    if (!living) damage.direct = source;

    // Entities within twice the power: hurt and thrown by distance and how much they see.
    const float reach = power * 2.0f;
    auto blast = [&](Entity& e, double eyeY) -> std::optional<glm::dvec3> {
        const glm::dvec3 offset = e.pos - center;
        const double distance01 = std::sqrt(glm::dot(offset, offset)) / double(reach);
        if (distance01 > 1.0) return std::nullopt;
        glm::dvec3 dir(e.pos.x - center.x, eyeY - center.y, e.pos.z - center.z);
        const double length = std::sqrt(glm::dot(dir, dir));
        if (length == 0.0) return std::nullopt;
        dir /= length;
        const float seen = seenPercent(level, center, e.box());
        e.hurtServer(level, damage, damageAt(power, distance01, seen));
        return dir * ((1.0 - distance01) * double(seen));
    };
    const AABB area(std::floor(center.x - reach - 1.0), std::floor(center.y - reach - 1.0), std::floor(center.z - reach - 1.0),
                    std::floor(center.x + reach + 1.0), std::floor(center.y + reach + 1.0), std::floor(center.z + reach + 1.0));
    for (Entity* e : level.entitiesIn(area, source)) {
        const bool tnt = dynamic_cast<PrimedTnt*>(e) != nullptr;
        if (auto push = blast(*e, tnt ? e->pos.y : e->pos.y + double(e->height) * 0.85)) e->velocity += *push;
    }
    if (Player* p = level.player(); p && !p->dead() && p->box().intersects(area) && p->mode() != GameMode::Spectator)
        if (auto push = blast(*p, p->eyePos().y); push && !(p->mode() == GameMode::Creative && p->flying))
            p->velocity += *push;

    // The blocks, in random order: their loot (merged into stacks of up to 16), their
    // containers' contents, and TNT lit on a short fuse.
    Xoroshiro& r = level.random();
    for (size_t i = blocks.size(); i > 1; --i) std::swap(blocks[i - 1], blocks[size_t(r.nextInt(int(i)))]);
    struct Collected {
        BlockPos pos;
        ItemStack stack;
    };
    std::vector<Collected> drops;
    for (const BlockPos& b : blocks) {
        const StateId s = level.getBlock(b);
        if (reg.info(s).flags & kAir) continue;
        const bool tnt = shortId(reg.blockOf(s).id) == "tnt";
        if (!tnt && level.doTileDrops && level.loot())
            for (ItemStack stack : level.loot()->drops(s, false, r)) {
                if (decay && r.nextFloat() > 1.0f / power) continue; // vanilla survives_explosion
                for (Collected& c : drops) {
                    if (!c.stack.sameItem(stack) || c.stack.count + stack.count > stack.maxStack()) continue;
                    const int moved = std::min(std::min(c.stack.maxStack(), 16) - c.stack.count, stack.count);
                    c.stack.count += moved;
                    stack.shrink(moved);
                    if (stack.empty()) break;
                }
                if (!stack.empty()) drops.push_back({b, stack});
            }
        if (auto be = level.blockEntities().find(b); be != level.blockEntities().end())
            for (const ItemStack& stack : be->second->contents()) ItemEntity::spill(level, b, stack);
        level.setBlockAndUpdate(b, BlockRegistry::kAirState);
        if (tnt) PrimedTnt::primeByExplosion(level, b);
    }
    for (const Collected& c : drops) ItemEntity::popResource(level, c.pos, c.stack);
    level.levelEvent(LevelEventType::Explosion, BlockPos::containing(center), 0, int(power), center);
}

} // namespace mc::sim
