#include "entity/Particles.h"

#include <algorithm>
#include <cmath>

#include "entity/Physics.h"

namespace mc {

std::string_view particleDefinition(ParticleType type) {
    switch (type) {
    case ParticleType::Terrain: return {};
    case ParticleType::Smoke: return "smoke";
    case ParticleType::LargeSmoke: return "large_smoke";
    case ParticleType::Flame: return "flame";
    case ParticleType::SmallFlame: return "small_flame";
    case ParticleType::SoulFireFlame: return "soul_fire_flame";
    case ParticleType::Lava: return "lava";
    case ParticleType::Rain: return "rain";
    case ParticleType::Splash: return "splash";
    case ParticleType::Bubble: return "bubble";
    case ParticleType::Underwater: return "underwater";
    case ParticleType::DrippingWater: return "dripping_water";
    case ParticleType::FallingWater: return "falling_water";
    case ParticleType::DrippingLava: return "dripping_lava";
    case ParticleType::FallingLava: return "falling_lava";
    case ParticleType::LandingLava: return "landing_lava";
    case ParticleType::Explosion: return "explosion";
    case ParticleType::ExplosionEmitter: return {}; // draws nothing itself
    case ParticleType::HappyVillager: return "happy_villager";
    case ParticleType::TintedLeaves: return "tinted_leaves";
    case ParticleType::CherryLeaves: return "cherry_leaves";
    case ParticleType::CampfireSmoke: return "campfire_cosy_smoke";
    case ParticleType::Count: break;
    }
    return {};
}

void ParticleSystem::setSprites(ParticleType type, std::vector<int> sprites) { sprites_[size_t(type)] = std::move(sprites); }

void ParticleSystem::push(Particle p) {
    if (ticking_) {
        spawned_.push_back(p);
        return;
    }
    if (particles_.size() >= kMax) particles_.erase(particles_.begin()); // vanilla drops the oldest
    particles_.push_back(p);
}

void ParticleSystem::pickSprite(Particle& p) {
    const auto& s = sprites_[size_t(p.type)];
    if (!s.empty()) p.sprite = s[size_t(random_.nextInt(int(s.size())))];
}

void ParticleSystem::spriteFromAge(Particle& p) {
    // Vanilla SpriteSet.get(age, lifetime): the frames played over the lifetime.
    const auto& s = sprites_[size_t(p.type)];
    if (!s.empty()) p.sprite = s[size_t(p.age * (int(s.size()) - 1) / std::max(p.lifetime, 1))];
}

Particle ParticleSystem::base(ParticleType type, const glm::dvec3& at) {
    // Vanilla Particle(level, x, y, z) and SingleQuadParticle: a 0.2 box, 0.1-0.2 across.
    Particle p;
    p.type = type;
    p.pos = p.oldPos = at;
    p.lifetime = int(4.0f / (random_.nextFloat() * 0.9f + 0.1f));
    p.size = 0.1f * (random_.nextFloat() * 0.5f + 0.5f) * 2.0f;
    return p;
}

Particle ParticleSystem::withSpeed(ParticleType type, const glm::dvec3& at, const glm::dvec3& speed) {
    // Vanilla Particle(level, x, y, z, xd, yd, zd): the speed jittered by up to 0.4, then
    // scaled to a random length, with a little lift.
    Particle p = base(type, at);
    glm::dvec3 v = speed + glm::dvec3(random_.nextDouble() * 2.0 - 1.0, random_.nextDouble() * 2.0 - 1.0,
                                      random_.nextDouble() * 2.0 - 1.0) *
                               double(0.4f);
    const double d = (random_.nextDouble() + random_.nextDouble() + 1.0) * double(0.15f);
    const double len = std::max(glm::length(v), 1e-9);
    v = v / len * d * double(0.4f);
    v.y += double(0.1f);
    p.vel = v;
    return p;
}

void ParticleSystem::add(ParticleType type, const glm::dvec3& at, const glm::dvec3& speed, const glm::vec3& color) {
    if (type != ParticleType::ExplosionEmitter && sprites_[size_t(type)].empty()) return;
    Particle p;
    auto jitter = [&] { return double((random_.nextFloat() - random_.nextFloat()) * 0.05f); };
    switch (type) {
    case ParticleType::Smoke:
    case ParticleType::LargeSmoke: {
        // Vanilla BaseAshSmokeParticle (SmokeParticle; large smoke at 2.5x).
        const float scale = type == ParticleType::LargeSmoke ? 2.5f : 1.0f;
        p = withSpeed(type, at, glm::dvec3(0.0));
        p.friction = 0.96f;
        p.gravity = -0.1f;
        p.speedUpWhenBlocked = true;
        p.vel = p.vel * 0.1 + speed;
        p.color = glm::vec3(random_.nextFloat() * 0.3f);
        p.size *= 0.75f * scale;
        p.lifetime = std::max(1, int(8.0 / (random_.nextFloat() * 0.8 + 0.2) * scale));
        spriteFromAge(p);
        break;
    }
    case ParticleType::Flame:
    case ParticleType::SmallFlame:
    case ParticleType::SoulFireFlame:
        // Vanilla RisingParticle/FlameParticle: drifts by its speed, no collisions.
        p = withSpeed(type, at, speed);
        p.friction = 0.96f;
        p.vel = p.vel * 0.01 + speed;
        p.pos.x += jitter();
        p.pos.y += jitter();
        p.pos.z += jitter();
        p.oldPos = p.pos;
        p.lifetime = int(8.0 / (random_.nextDouble() * 0.8 + 0.2)) + 4;
        p.hasPhysics = false;
        if (type == ParticleType::SmallFlame) {
            p.size *= 0.5f;
            p.width = 0.1f;
        }
        pickSprite(p);
        break;
    case ParticleType::Lava:
        p = withSpeed(type, at, glm::dvec3(0.0));
        p.gravity = 0.75f;
        p.friction = 0.999f;
        p.vel *= double(0.8f);
        p.vel.y = double(random_.nextFloat() * 0.4f + 0.05f);
        p.size *= random_.nextFloat() * 2.0f + 0.2f;
        p.lifetime = int(16.0 / (random_.nextDouble() * 0.8 + 0.2));
        pickSprite(p);
        break;
    case ParticleType::Rain:
    case ParticleType::Splash:
        // Vanilla WaterDropParticle (and SplashParticle, thrown by `speed`).
        p = withSpeed(type, at, glm::dvec3(0.0));
        p.vel.x *= double(0.3f);
        p.vel.y = random_.nextDouble() * double(0.2f) + double(0.1f);
        p.vel.z *= double(0.3f);
        p.width = 0.01f;
        p.gravity = 0.06f;
        p.lifetime = int(8.0 / (random_.nextDouble() * 0.8 + 0.2));
        if (type == ParticleType::Splash) {
            p.gravity = 0.04f;
            if (speed.y == 0.0 && (speed.x != 0.0 || speed.z != 0.0)) p.vel = {speed.x, 0.1, speed.z};
        }
        pickSprite(p);
        break;
    case ParticleType::Bubble:
        p = base(type, at);
        p.width = 0.02f;
        p.size *= random_.nextFloat() * 0.6f + 0.2f;
        p.vel = speed * double(0.2f) + glm::dvec3(random_.nextDouble() * 2.0 - 1.0, random_.nextDouble() * 2.0 - 1.0,
                                                  random_.nextDouble() * 2.0 - 1.0) *
                                           double(0.02f);
        p.lifetime = int(8.0 / (random_.nextDouble() * 0.8 + 0.2));
        pickSprite(p);
        break;
    case ParticleType::Underwater:
        // Vanilla SuspendedParticle: motes hanging still.
        p = base(type, at - glm::dvec3(0.0, 0.125, 0.0));
        p.width = 0.01f;
        pickSprite(p);
        p.size *= random_.nextFloat() * 0.6f + 0.2f;
        p.lifetime = int(16.0 / (random_.nextDouble() * 0.8 + 0.2));
        p.hasPhysics = false;
        p.friction = 1.0f;
        p.color = {0.4f, 0.4f, 0.7f};
        break;
    case ParticleType::DrippingWater:
    case ParticleType::DrippingLava:
    case ParticleType::FallingWater:
    case ParticleType::FallingLava:
    case ParticleType::LandingLava: {
        // Vanilla DripParticle: hanging for 2 seconds, falling, landing.
        p = base(type, at);
        p.width = 0.01f;
        p.gravity = 0.06f;
        const bool lava = type == ParticleType::DrippingLava || type == ParticleType::FallingLava ||
                          type == ParticleType::LandingLava;
        p.color = lava ? glm::vec3(1.0f, 0.2857143f, 0.083333336f) : glm::vec3(0.2f, 0.3f, 1.0f);
        if (type == ParticleType::DrippingWater || type == ParticleType::DrippingLava) {
            p.gravity *= 0.02f;
            p.lifetime = 40;
        } else if (type == ParticleType::LandingLava) {
            p.lifetime = int(16.0 / (random_.nextDouble() * 0.8 + 0.2));
        } else {
            p.lifetime = int(64.0 / (random_.nextDouble() * 0.8 + 0.2));
        }
        pickSprite(p);
        break;
    }
    case ParticleType::Explosion:
        // Vanilla HugeExplosionParticle: its size from the x argument.
        p = withSpeed(type, at, glm::dvec3(0.0));
        p.vel = glm::dvec3(0.0);
        p.lifetime = 6 + random_.nextInt(4);
        p.color = glm::vec3(random_.nextFloat() * 0.6f + 0.4f);
        p.size = 2.0f * (1.0f - float(speed.x) * 0.5f);
        p.hasPhysics = false;
        spriteFromAge(p);
        break;
    case ParticleType::ExplosionEmitter:
        p = base(type, at);
        p.lifetime = 8;
        p.hasPhysics = false;
        p.size = 0.0f;
        break;
    case ParticleType::HappyVillager:
        // Vanilla SuspendedTownParticle.
        p = withSpeed(type, at, speed);
        p.width = 0.02f;
        p.size *= random_.nextFloat() * 0.6f + 0.5f;
        p.vel *= double(0.02f);
        p.lifetime = int(20.0 / (random_.nextDouble() * 0.8 + 0.2));
        p.hasPhysics = false;
        pickSprite(p);
        break;
    case ParticleType::TintedLeaves:
    case ParticleType::CherryLeaves: {
        // Vanilla FallingLeavesParticle: tinted leaves swirl, cherry petals drift away.
        const bool cherry = type == ParticleType::CherryLeaves;
        const float fall = cherry ? 0.25f : 0.07f, wind = cherry ? 2.0f : 10.0f, scale = cherry ? 1.0f : 2.0f;
        p = base(type, at);
        pickSprite(p);
        p.rotSpeed = float(glm::radians(random_.nextBool() ? -30.0 : 30.0));
        const float r = random_.nextFloat();
        p.spin = float(glm::radians(random_.nextBool() ? -5.0 : 5.0));
        p.swirls = !cherry;
        p.flowAway = cherry;
        p.lifetime = 300;
        p.gravity = fall * 1.2f * 0.0025f;
        p.size = scale * (random_.nextBool() ? 0.05f : 0.075f);
        p.width = p.size;
        p.friction = 1.0f;
        p.vel = {0.0, -(cherry ? 0.0 : double(0.021f)), 0.0};
        p.flowX = float(std::cos(glm::radians(double(r) * 60.0)) * wind);
        p.flowZ = float(std::sin(glm::radians(double(r) * 60.0)) * wind);
        p.swirl = float(glm::radians(1000.0 + double(r) * 3000.0));
        p.wind = wind;
        p.color = color;
        push(p);
        return;
    }
    case ParticleType::CampfireSmoke:
        // Vanilla CampfireSmokeParticle: a big slow puff that rises for 4-6 seconds.
        p = base(type, at);
        p.size *= 3.0f;
        p.width = 0.25f;
        p.lifetime = random_.nextInt(50) + 80;
        p.gravity = 3.0e-6f;
        p.vel = {speed.x, speed.y + double(random_.nextFloat() / 500.0f), speed.z};
        p.alpha = 1.0f;
        pickSprite(p);
        break;
    case ParticleType::Terrain:
    case ParticleType::Count: return;
    }
    if (color != glm::vec3(1.0f)) p.color = color;
    push(p);
}

Particle ParticleSystem::terrainParticle(const glm::dvec3& p, const glm::dvec3& dir, int sprite, uint32_t tint) {
    Particle q = withSpeed(ParticleType::Terrain, p, dir);
    q.gravity = 1.0f;
    q.size /= 2.0f; // vanilla TerrainParticle halves its quadSize
    q.sprite = sprite;
    // A random quarter of the sprite.
    const float uo = random_.nextFloat() * 3.0f, vo = random_.nextFloat() * 3.0f;
    q.uv = {uo / 4.0f, vo / 4.0f, (uo + 1.0f) / 4.0f, (vo + 1.0f) / 4.0f};
    q.color = glm::vec3(0.6f) * glm::vec3(float((tint >> 16) & 255), float((tint >> 8) & 255), float(tint & 255)) / 255.0f;
    return q;
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
        push(q);
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
                    push(terrainParticle(origin + b.min + t * size, t - 0.5, sprite, tint));
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
    push(q);
}

void ParticleSystem::move(const BlockGetter& level, Particle& p) {
    // Vanilla Particle.move: its box against block collision shapes (unless physics are off).
    glm::dvec3 moved = p.vel;
    if (p.hasPhysics && moved != glm::dvec3(0.0) && glm::dot(moved, moved) < 100.0 * 100.0) {
        const double h = double(p.width) / 2.0;
        const AABB box(p.pos.x - h, p.pos.y, p.pos.z - h, p.pos.x + h, p.pos.y + double(p.width), p.pos.z + h);
        moved = collideBoundingBox(level, box, p.vel);
    }
    p.pos += moved;
    p.onGround = p.vel.y != moved.y && p.vel.y < 0.0;
    if (moved.x != p.vel.x) p.vel.x = 0.0;
    if (moved.z != p.vel.z) p.vel.z = 0.0;
}

namespace {

// Vanilla FluidState.getHeight: how high the fluid of `kind` fills its cell (0 if another).
double fluidHeight(const BlockGetter& level, const BlockPos& p, FluidKind kind) {
    const FluidState f = level.fluid(p);
    if (f.kind != kind) return 0.0;
    if (level.fluid(p.above()).kind == kind) return 1.0;
    return double(f.falling || f.source() ? 8 : 8 - f.level) / 9.0;
}

FluidKind dripFluid(ParticleType t) {
    switch (t) {
    case ParticleType::DrippingWater:
    case ParticleType::FallingWater: return FluidKind::Water;
    case ParticleType::DrippingLava:
    case ParticleType::FallingLava:
    case ParticleType::LandingLava: return FluidKind::Lava;
    default: return FluidKind::None;
    }
}

} // namespace

bool ParticleSystem::tickOne(const BlockGetter& level, Particle& p) {
    const BlockRegistry& reg = level.registry();
    p.oldPos = p.pos;
    p.oldRoll = p.roll;
    switch (p.type) {
    case ParticleType::Rain:
    case ParticleType::Splash: {
        if (p.lifetime-- <= 0) return false;
        p.vel.y -= double(p.gravity);
        move(level, p);
        p.vel *= 0.98;
        if (p.onGround) {
            if (random_.nextDouble() < 0.5) return false;
            p.vel.x *= 0.7;
            p.vel.z *= 0.7;
        }
        // Gone once inside a block's top or a fluid.
        const BlockPos b = BlockPos::containing(p.pos);
        double top = 0.0;
        for (const AABB& box : reg.collisionShape(level.getBlock(b)))
            if (p.pos.x - b.x >= box.min.x && p.pos.x - b.x <= box.max.x && p.pos.z - b.z >= box.min.z &&
                p.pos.z - b.z <= box.max.z)
                top = std::max(top, box.max.y);
        const FluidState f = level.fluid(b);
        if (!f.empty()) top = std::max(top, fluidHeight(level, b, f.kind));
        return !(top > 0.0 && p.pos.y < b.y + top);
    }
    case ParticleType::Bubble:
        if (p.lifetime-- <= 0) return false;
        p.vel.y += 0.002;
        move(level, p);
        p.vel *= double(0.85f);
        return level.fluid(BlockPos::containing(p.pos)).kind == FluidKind::Water;
    case ParticleType::HappyVillager:
        if (p.lifetime-- <= 0) return false;
        move(level, p);
        p.vel *= 0.99;
        return true;
    case ParticleType::Explosion:
        if (p.age++ >= p.lifetime) return false;
        spriteFromAge(p);
        return true;
    case ParticleType::ExplosionEmitter:
        for (int i = 0; i < 6; ++i) {
            const double a = random_.nextDouble(), b = random_.nextDouble(), c = random_.nextDouble(), d = random_.nextDouble(),
                         e = random_.nextDouble(), f = random_.nextDouble();
            add(ParticleType::Explosion, p.pos + glm::dvec3(a - b, c - d, e - f) * 4.0,
                glm::dvec3(double(float(p.age) / float(p.lifetime)), 0.0, 0.0));
        }
        return ++p.age != p.lifetime;
    case ParticleType::DrippingWater:
    case ParticleType::DrippingLava:
    case ParticleType::FallingWater:
    case ParticleType::FallingLava:
    case ParticleType::LandingLava: {
        const bool hanging = p.type == ParticleType::DrippingWater || p.type == ParticleType::DrippingLava;
        if (p.type == ParticleType::DrippingLava) // cooling from white-hot to orange
            p.color = {1.0f, 16.0f / float(40 - p.lifetime + 16), 4.0f / float(40 - p.lifetime + 8)};
        if (p.lifetime-- <= 0) {
            if (hanging)
                add(p.type == ParticleType::DrippingWater ? ParticleType::FallingWater : ParticleType::FallingLava, p.pos,
                    p.vel);
            return false;
        }
        p.vel.y -= double(p.gravity);
        move(level, p);
        if (hanging) {
            p.vel *= double(0.02f);
        } else if (p.onGround && p.type != ParticleType::LandingLava) {
            if (p.type == ParticleType::FallingWater) add(ParticleType::Splash, p.pos);
            else add(ParticleType::LandingLava, p.pos);
            return false;
        }
        p.vel *= double(0.98f);
        const BlockPos b = BlockPos::containing(p.pos);
        const FluidKind kind = dripFluid(p.type);
        return !(level.fluid(b).kind == kind && p.pos.y < b.y + fluidHeight(level, b, kind));
    }
    case ParticleType::TintedLeaves:
    case ParticleType::CherryLeaves: {
        if (p.lifetime-- <= 0) return false;
        const float g = std::min(float(300 - p.lifetime) / 300.0f, 1.0f);
        double dx = 0.0, dz = 0.0;
        if (p.flowAway) {
            dx += double(p.flowX) * std::pow(double(g), 1.25);
            dz += double(p.flowZ) * std::pow(double(g), 1.25);
        }
        if (p.swirls) {
            dx += double(g) * std::cos(double(g) * double(p.swirl)) * double(p.wind);
            dz += double(g) * std::sin(double(g) * double(p.swirl)) * double(p.wind);
        }
        p.vel.x += dx * double(0.0025f);
        p.vel.z += dz * double(0.0025f);
        p.vel.y -= double(p.gravity);
        p.rotSpeed += p.spin / 20.0f;
        p.roll += p.rotSpeed / 20.0f;
        move(level, p);
        if (p.onGround || (p.lifetime < 299 && (p.vel.x == 0.0 || p.vel.z == 0.0))) return false;
        p.vel *= double(p.friction);
        return true;
    }
    case ParticleType::CampfireSmoke:
        if (p.age++ >= p.lifetime || p.alpha <= 0.0f) return false;
        p.vel.x += double(random_.nextFloat() / 5000.0f * (random_.nextBool() ? 1.0f : -1.0f));
        p.vel.z += double(random_.nextFloat() / 5000.0f * (random_.nextBool() ? 1.0f : -1.0f));
        p.vel.y -= double(p.gravity);
        move(level, p);
        if (p.age >= p.lifetime - 60 && p.alpha > 0.01f) p.alpha -= 0.015f;
        return true;
    default: break;
    }
    // Vanilla Particle.tick.
    if (p.age++ >= p.lifetime) return false;
    p.vel.y -= 0.04 * double(p.gravity);
    const double y0 = p.pos.y;
    move(level, p);
    if (p.speedUpWhenBlocked && p.pos.y == y0) {
        p.vel.x *= 1.1;
        p.vel.z *= 1.1;
    }
    p.vel *= double(p.friction);
    if (p.onGround) {
        p.vel.x *= double(0.7f);
        p.vel.z *= double(0.7f);
    }
    if (p.type == ParticleType::Smoke || p.type == ParticleType::LargeSmoke) spriteFromAge(p);
    // Lava pops trail smoke, less as they age.
    if (p.type == ParticleType::Lava && random_.nextFloat() > float(p.age) / float(p.lifetime))
        add(ParticleType::Smoke, p.pos, p.vel);
    return true;
}

void ParticleSystem::tick(const BlockGetter& level) {
    ticking_ = true;
    for (Particle& p : particles_)
        if (!tickOne(level, p)) p.lifetime = -1000; // marked for removal
    ticking_ = false;
    std::erase_if(particles_, [](const Particle& p) { return p.lifetime == -1000; });
    for (Particle& p : spawned_) push(p);
    spawned_.clear();
}

float ParticleSystem::quadSize(const Particle& p, float pt) {
    const float f = (float(p.age) + pt) / float(std::max(p.lifetime, 1));
    switch (p.type) {
    case ParticleType::Smoke:
    case ParticleType::LargeSmoke: return p.size * std::clamp(f * 32.0f, 0.0f, 1.0f);
    case ParticleType::Flame:
    case ParticleType::SmallFlame:
    case ParticleType::SoulFireFlame: return p.size * (1.0f - f * f * 0.5f);
    case ParticleType::Lava: return p.size * (1.0f - f * f);
    default: return p.size;
    }
}

glm::vec2 ParticleSystem::light(const Particle& p, glm::vec2 cell, float pt) {
    switch (p.type) {
    case ParticleType::Flame:
    case ParticleType::SmallFlame:
    case ParticleType::SoulFireFlame: {
        // Brightening to full block light as they burn out.
        const float f = std::clamp((float(p.age) + pt) / float(std::max(p.lifetime, 1)), 0.0f, 1.0f);
        return {std::min(15.0f, cell.x + f * 15.0f), cell.y};
    }
    case ParticleType::Lava:
    case ParticleType::DrippingLava:
    case ParticleType::FallingLava:
    case ParticleType::LandingLava: return {15.0f, cell.y};
    case ParticleType::Explosion: return {15.0f, 15.0f};
    default: return cell;
    }
}

} // namespace mc
