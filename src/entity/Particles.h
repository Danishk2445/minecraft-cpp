// Particles.h -- vanilla TerrainParticle: block-breaking debris and the chips that
// fly off a block while it is mined. Simulation only; render/ParticleRenderer draws them.
#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "core/Rng.h"
#include "world/block/BlockGetter.h"

namespace mc {

struct Particle {
    glm::dvec3 pos{0.0}, oldPos{0.0}, vel{0.0};
    float size = 0.1f; // half the quad's side, blocks
    int age = 0, lifetime = 0;
    float gravity = 1.0f;
    bool onGround = false;
    int sprite = 0;               // atlas sprite
    glm::vec4 uv{0.0f};           // sub-rectangle of the sprite (u0, v0, u1, v1), 0..1
    glm::vec3 color{0.6f};
};

class ParticleSystem {
public:
    static constexpr size_t kMax = 16384; // vanilla's per-type limit

    // Debris for a broken block: a grid over each box of its outline (vanilla ParticleEngine.destroy).
    void addBlockBreak(const BlockRegistry& reg, StateId state, const BlockPos& pos, int sprite, uint32_t tint);
    // One chip off `face` of a block being mined (vanilla ParticleEngine.crack).
    void addBlockHit(const BlockRegistry& reg, StateId state, const BlockPos& pos, Direction face, int sprite,
                     uint32_t tint);
    // Moves, collides and ages every particle; removes the expired ones.
    void tick(const BlockGetter& level);
    const std::vector<Particle>& particles() const { return particles_; }

private:
    Particle terrainParticle(const glm::dvec3& p, const glm::dvec3& dir, int sprite, uint32_t tint);
    void add(Particle p);

    std::vector<Particle> particles_;
    Xoroshiro random_{0x5EEDu};
};

} // namespace mc
