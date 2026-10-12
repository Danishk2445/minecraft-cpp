#include "entity/Particles.h"

#include <algorithm>
#include <cmath>

#include "entity/Physics.h"

namespace mc {

Particle ParticleSystem::terrainParticle(const glm::dvec3& p, const glm::dvec3& dir, int sprite, uint32_t tint) {
    Particle q;
    q.pos = q.oldPos = p;
    // Vanilla Particle(level, x, y, z, xa, ya, za): jitter, normalize to a random speed, a little lift.
    glm::dvec3 v = dir + glm::dvec3(random_.nextDouble() * 2.0 - 1.0, random_.nextDouble() * 2.0 - 1.0,
                                    random_.nextDouble() * 2.0 - 1.0) *
                             0.4;
    const double speed = (random_.nextDouble() + random_.nextDouble() + 1.0) * 0.15;
    const double len = std::max(glm::length(v), 1e-9);
    v = v / len * speed * 0.4;
    v.y += 0.1;
    q.vel = v;
    q.lifetime = int(4.0f / (random_.nextFloat() * 0.9f + 0.1f));
    q.size = 0.1f * (random_.nextFloat() * 0.5f + 0.5f); // vanilla quadSize (0.2 * ...) halved for terrain
    q.gravity = 1.0f;
    q.sprite = sprite;
    // A random quarter of the sprite.
    const float uo = random_.nextFloat() * 3.0f, vo = random_.nextFloat() * 3.0f;
    q.uv = {uo / 4.0f, vo / 4.0f, (uo + 1.0f) / 4.0f, (vo + 1.0f) / 4.0f};
    q.color = glm::vec3(0.6f) * glm::vec3(float((tint >> 16) & 255), float((tint >> 8) & 255), float(tint & 255)) / 255.0f;
    return q;
}

void ParticleSystem::add(Particle p) {
    if (particles_.size() >= kMax) particles_.erase(particles_.begin()); // vanilla drops the oldest
    particles_.push_back(p);
}

void ParticleSystem::addItemParticles(const glm::dvec3& eye, float yaw, float pitch, int sprite, int count) {
    // Vanilla Vec3.xRot(-pitch) then yRot(-yaw).
    const double xr = glm::radians(double(-pitch)), yr = glm::radians(double(-yaw));
    auto rotate = [&](glm::dvec3 v) {
        v = {v.x, v.y * std::cos(xr) + v.z * std::sin(xr), v.z * std::cos(xr) - v.y * std::sin(xr)};
        return glm::dvec3(v.x * std::cos(yr) + v.z * std::sin(yr), v.y, v.z * std::cos(yr) - v.x * std::sin(yr));
    };
    for (int i = 0; i < count; ++i) {
        const glm::dvec3 v = rotate({(random_.nextFloat() - 0.5) * 0.1, random_.nextDouble() * 0.1 + 0.1, 0.0});
        const double down = -random_.nextFloat() * 0.6 - 0.3;
        const glm::dvec3 at = rotate({(random_.nextFloat() - 0.5) * 0.3, down, 0.6}) + eye;
        Particle q = terrainParticle(at, glm::dvec3(0.0), sprite, 0xFFFFFF);
        q.vel = q.vel * 0.1 + glm::dvec3(v.x, v.y + 0.05, v.z);
        q.color = glm::vec3(1.0f);
        add(q);
    }
}

void ParticleSystem::addBlockBreak(const BlockRegistry& reg, StateId state, const BlockPos& pos, int sprite,
                                   uint32_t tint) {
    const Shape& shape = reg.outlineShape(state);
    const glm::dvec3 origin(pos.vec());
    for (const AABB& b : shape) {
        const glm::dvec3 size = glm::min(b.size(), glm::dvec3(1.0));
        const int nx = std::max(2, int(std::ceil(size.x / 0.25))), ny = std::max(2, int(std::ceil(size.y / 0.25)));
        const int nz = std::max(2, int(std::ceil(size.z / 0.25)));
        for (int i = 0; i < nx; ++i)
            for (int j = 0; j < ny; ++j)
                for (int k = 0; k < nz; ++k) {
                    const glm::dvec3 t((i + 0.5) / nx, (j + 0.5) / ny, (k + 0.5) / nz);
                    add(terrainParticle(origin + b.min + t * size, t - 0.5, sprite, tint));
                }
    }
}

void ParticleSystem::addBlockHit(const BlockRegistry& reg, StateId state, const BlockPos& pos, Direction face, int sprite,
                                 uint32_t tint) {
    const Shape& shape = reg.outlineShape(state);
    if (shape.empty()) return;
    AABB b = shape[0];
    for (const AABB& a : shape) b = AABB(glm::min(b.min, a.min), glm::max(b.max, a.max));
    constexpr double kMargin = 0.1;
    glm::dvec3 p = glm::dvec3(pos.vec()) +
                   glm::dvec3(random_.nextDouble() * (b.max.x - b.min.x - kMargin * 2) + kMargin + b.min.x,
                              random_.nextDouble() * (b.max.y - b.min.y - kMargin * 2) + kMargin + b.min.y,
                              random_.nextDouble() * (b.max.z - b.min.z - kMargin * 2) + kMargin + b.min.z);
    const int a = int(axisOf(face));
    p[a] = pos.vec()[a] + (isPositive(face) ? b.max[a] + kMargin : b.min[a] - kMargin);
    Particle q = terrainParticle(p, glm::dvec3(0.0), sprite, tint);
    // Vanilla setPower(0.2).scale(0.6).
    q.vel.x *= 0.2;
    q.vel.z *= 0.2;
    q.vel.y = (q.vel.y - 0.1) * 0.2 + 0.1;
    q.size *= 0.6f;
    add(q);
}

void ParticleSystem::tick(const BlockGetter& level) {
    for (Particle& p : particles_) {
        p.oldPos = p.pos;
        if (++p.age >= p.lifetime) continue;
        p.vel.y -= 0.04 * p.gravity;
        // Vanilla Particle.move: a 0.2-block box against block collision shapes.
        const AABB box(p.pos.x - 0.1, p.pos.y, p.pos.z - 0.1, p.pos.x + 0.1, p.pos.y + 0.2, p.pos.z + 0.1);
        const glm::dvec3 moved = collideBoundingBox(level, box, p.vel);
        p.pos += moved;
        p.onGround = p.vel.y != moved.y && p.vel.y < 0.0;
        if (moved.x != p.vel.x) p.vel.x = 0.0;
        if (moved.z != p.vel.z) p.vel.z = 0.0;
        p.vel *= 0.98;
        if (p.onGround) {
            p.vel.x *= 0.7;
            p.vel.z *= 0.7;
        }
    }
    std::erase_if(particles_, [](const Particle& p) { return p.age >= p.lifetime; });
}

} // namespace mc
