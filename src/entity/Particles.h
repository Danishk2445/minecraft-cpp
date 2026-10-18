// Particles.h -- the client's particles (vanilla ParticleEngine and the particle classes):
// block debris and mining chips (TerrainParticle), smoke, flames, lava pops, rain
// splashes, bubbles, drips, explosions, bone-meal sparkles, falling leaves and campfire
// smoke. Simulation only; render/ParticleRenderer draws them.
#pragma once

#include <array>
#include <string_view>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "core/Rng.h"
#include "world/block/BlockGetter.h"

#include "game/Options.h"

namespace mc {

enum class ParticleType : uint8_t {
    Terrain, // block and item debris (block-atlas sprites)
    Smoke,
    LargeSmoke,
    Flame,
    SmallFlame,
    SoulFireFlame,
    Lava,
    Rain,          // a raindrop splashing (vanilla WaterDropParticle)
    Splash,        // water thrown up by something falling in
    Bubble,
    Underwater,    // motes suspended in water
    DrippingWater, // hanging under a block, then falling and splashing
    FallingWater,
    DrippingLava,
    FallingLava,
    LandingLava,
    Explosion,
    ExplosionEmitter, // spawns explosions around it for 8 ticks
    HappyVillager,    // bone meal's green sparkles
    TintedLeaves,     // falling from leaves (colored like them)
    CherryLeaves,
    CampfireSmoke,
    Poof,            // a dead mob's smoke (vanilla ExplodeParticle)
    Heart,           // animals in love
    Crit,            // critical hits, critical arrows
    DamageIndicator, // dark hearts from a hard hit
    SweepAttack,     // a sword's sweep
    Count,
};
// The particles/<name>.json definition each type draws its sprites from.
std::string_view particleDefinition(ParticleType type);

struct Particle {
    ParticleType type = ParticleType::Terrain;
    glm::dvec3 pos{0.0}, oldPos{0.0}, vel{0.0};
    float size = 0.1f;   // half the quad's side, blocks (vanilla quadSize)
    float width = 0.2f;  // collision box (vanilla setSize)
    int age = 0, lifetime = 0;
    float gravity = 0.0f;
    float friction = 0.98f;
    bool hasPhysics = true;
    bool speedUpWhenBlocked = false;
    bool onGround = false;
    int sprite = 0;     // atlas sprite
    glm::vec4 uv{0.0f, 0.0f, 1.0f, 1.0f}; // sub-rectangle of the sprite (u0, v0, u1, v1), 0..1
    glm::vec3 color{1.0f};
    float alpha = 1.0f;
    float roll = 0.0f, oldRoll = 0.0f; // falling leaves turn
    float rotSpeed = 0.0f, spin = 0.0f, flowX = 0.0f, flowZ = 0.0f, swirl = 0.0f, wind = 0.0f;
    bool flowAway = false, swirls = false;
};

class ParticleSystem {
public:
    static constexpr size_t kMax = 16384; // vanilla's per-type limit

    // Vanilla LevelRenderer.addParticleInternal: with a camera, add() skips particles more
    // than 32 blocks from it, a third of them on Decreased and all on Minimal. Block
    // debris and item pieces always show.
    ParticleMode mode = ParticleMode::All;
    std::optional<glm::dvec3> camera;

    // The atlas sprites of each type (from particles/*.json); types without any are skipped.
    void setSprites(ParticleType type, std::vector<int> sprites);
    // Vanilla ParticleEngine.createParticle: a particle of `type` at `at`, `speed` being
    // the type's speed arguments (explosions read x as their size).
    void add(ParticleType type, const glm::dvec3& at, const glm::dvec3& speed = glm::dvec3(0.0),
             const glm::vec3& color = glm::vec3(1.0f));

    // Debris for a broken block: a grid over each box of its outline (vanilla ParticleEngine.destroy).
    void addBlockBreak(const BlockRegistry& reg, StateId state, const BlockPos& pos, int sprite, uint32_t tint);
    // One chip off `face` of a block being mined (vanilla ParticleEngine.crack).
    void addBlockHit(const BlockRegistry& reg, StateId state, const BlockPos& pos, Direction face, int sprite,
                     uint32_t tint);
    // Pieces of an item flying out in front of the eyes (vanilla LivingEntity.spawnItemParticles
    // with BreakingItemParticle): a tool breaking, food being eaten.
    void addItemParticles(const glm::dvec3& eye, float yaw, float pitch, int sprite, int count);
    // Moves, collides and ages every particle; removes the expired ones.
    void tick(const BlockGetter& level);
    const std::vector<Particle>& particles() const { return particles_; }
    Xoroshiro& random() { return random_; }

    // How a particle draws at `partialTick`: its half size, its light (block, sky; from
    // the cell's `light`, brightened for glowing ones), and whether it blends.
    static float quadSize(const Particle& p, float partialTick);
    static glm::vec2 light(const Particle& p, glm::vec2 cellLight, float partialTick);
    static bool translucent(const Particle& p) { return p.type == ParticleType::CampfireSmoke; }

private:
    Particle terrainParticle(const glm::dvec3& p, const glm::dvec3& dir, int sprite, uint32_t tint);
    // Vanilla Particle(level, x, y, z[, xd, yd, zd]): the base lifetime and random jitter.
    Particle base(ParticleType type, const glm::dvec3& at);
    Particle withSpeed(ParticleType type, const glm::dvec3& at, const glm::dvec3& speed);
    void push(Particle p);
    void pickSprite(Particle& p);
    void spriteFromAge(Particle& p);
    void move(const BlockGetter& level, Particle& p);
    bool tickOne(const BlockGetter& level, Particle& p); // false: remove it

    std::vector<Particle> particles_;
    std::vector<Particle> spawned_; // added during a tick
    bool ticking_ = false;
    std::array<std::vector<int>, size_t(ParticleType::Count)> sprites_;
    Xoroshiro random_{0x5EEDu};
};

} // namespace mc
