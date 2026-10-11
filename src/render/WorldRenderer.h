// WorldRenderer.h -- turns mesh-ready chunks into GPU meshes and draws the world.
#pragma once

#include <array>
#include <memory>
#include <unordered_map>

#include "core/JobSystem.h"
#include "gfx/Camera.h"
#include "render/ChunkRenderer.h"
#include "render/Environment.h"
#include "render/SkyRenderer.h"
#include "world/chunk/ChunkManager.h"
#include "world/mesh/ChunkMesher.h"

namespace mc {

class BiomeRegistry;
class ClientAssets;
class Level;

class WorldRenderer {
public:
    WorldRenderer(const ClientAssets& assets, const BlockRegistry& registry, const BiomeRegistry* biomes = nullptr);
    ~WorldRenderer();
    bool init();

    // Main thread, once per frame: drops unloaded chunks and schedules mesh jobs.
    void update(ChunkManager& chunks, JobSystem& jobs);
    void tick(); // 20 Hz: light flicker
    // Clears to the fog color and draws sky, terrain and clouds.
    void render(const Camera& camera, glm::ivec2 viewport, int renderDistance, const Level& level, float partialTick);
    const EnvironmentState& environment() const { return env_; }
    float gamma = 0.5f; // the Brightness option (0 = Moody, 1 = Bright)

    // No mesh jobs running.
    bool idle() const { return inFlight_ == 0; }
    const ChunkRenderer& chunkRenderer() const { return chunks_; }
    MesherOptions& mesherOptions() { return options_; }

private:
    const ClientAssets& assets_;
    const BlockRegistry& registry_;
    const BiomeRegistry* biomes_ = nullptr;
    ChunkRenderer chunks_;
    glm::dvec3 cameraPos_{0.0};
    SkyRenderer sky_;
    Lightmap lightmap_;
    LightFlicker flicker_;
    EnvironmentState env_;
    MesherOptions options_;
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);
    int inFlight_ = 0;
    uint64_t nextSeq_ = 1;
    // Latest submitted mesh sequence per chunk section; older results are discarded.
    std::unordered_map<ChunkPos, std::array<uint64_t, kSectionCount>> latest_;
    std::vector<ChunkManager::MeshWork> work_;
};

} // namespace mc
