#include "entity/LightningBolt.h"

#include "entity/Player.h"
#include "entity/mob/Mob.h"
#include "world/level/Level.h"
#include "world/sim/Fire.h"

namespace mc {

LightningBolt::LightningBolt() {
    width = 0.0f;
    height = 0.0f;
    noPhysics = true;
}

LightningBolt& LightningBolt::strike(Level& level, const BlockPos& pos, bool visualOnly) {
    auto e = std::make_unique<LightningBolt>();
    e->pos = e->oldPos = glm::dvec3(pos.x + 0.5, pos.y, pos.z + 0.5);
    e->seed = level.random().nextLong();
    e->flashes = level.random().nextInt(3) + 1;
    e->visualOnly = visualOnly;
    return static_cast<LightningBolt&>(level.addEntity(std::move(e)));
}

void LightningBolt::spawnFire(Level& level, int extraIgnitions) {
    if (visualOnly || !level.doFireTick) return;
    const BlockPos at = BlockPos::containing(pos);
    sim::ignite(level, at);
    Xoroshiro& r = level.random();
    for (int i = 0; i < extraIgnitions; ++i) {
        const int dx = r.nextInt(3) - 1;
        const int dy = r.nextInt(3) - 1;
        const int dz = r.nextInt(3) - 1;
        sim::ignite(level, at.offset(dx, dy, dz));
    }
}

void LightningBolt::tick(Level& level) {
    ++tickCount;
    oldPos = pos;
    if (life == 2) {
        // The strike: fires around it on Normal and Hard.
        if (level.difficulty == Difficulty::Normal || level.difficulty == Difficulty::Hard) spawnFire(level, 4);
    }
    --life;
    if (life < 0) {
        if (flashes == 0) {
            removed = true;
        } else if (life < -level.random().nextInt(10)) {
            // It strikes again.
            --flashes;
            life = 1;
            seed = level.random().nextLong();
            spawnFire(level, 0);
        }
    }
    if (life >= 0 && !visualOnly) {
        // Everything within 3 blocks (9 up) is struck: set alight and hurt by 5.
        const AABB area(pos.x - 3.0, pos.y - 3.0, pos.z - 3.0, pos.x + 3.0, pos.y + 6.0 + 3.0, pos.z + 3.0);
        for (Entity* e : level.entitiesIn(area, this)) {
            if (auto* mob = dynamic_cast<Mob*>(e)) {
                if (mob->alive()) mob->thunderHit(level);
            } else {
                e->hurtServer(level, DamageType::LightningBolt, 5.0f);
            }
        }
        if (Player* p = level.player(); p && !p->dead() && p->box().intersects(area)) {
            p->igniteForTicks(8 * 20);
            p->hurt(DamageType::LightningBolt, 5.0f);
        }
    }
}

} // namespace mc
