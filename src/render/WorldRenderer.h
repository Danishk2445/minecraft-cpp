// WorldRenderer.h -- turns mesh-ready chunks into GPU meshes and draws the world.
#pragma once

#include <array>
#include <memory>
#include <unordered_map>

#include "core/JobSystem.h"
#include "gfx/Camera.h"
#include "render/ChunkRenderer.h"
#include "world/chunk/ChunkManager.h"
#include "world/mesh/ChunkMesher.h"

namespace mc {

class ClientAssets;

class WorldRenderer {
public:
    WorldRenderer(const ClientAssets& assets, const BlockRegistry& registry);
    ~WorldRenderer();
    bool init();

    // Main thread, once per frame: drops unloaded chunks and schedules mesh jobs.
    void update(ChunkManager& chunks, JobSystem& jobs);
    void render(const Camera& camera, glm::ivec2 viewport, int renderDistance);

    // No mesh jobs running.
    bool idle() const { return inFlight_ == 0; }
    const ChunkRenderer& chunkRenderer() const { return chunks_; }
    MesherOptions& mesherOptions() { return options_; }

private:
    const ClientAssets& assets_;
    const BlockRegistry& registry_;
    ChunkRenderer chunks_;
    MesherOptions options_;
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);
    int inFlight_ = 0;
    uint64_t nextSeq_ = 1;
    // Latest submitted mesh sequence per chunk section; older results are discarded.
    std::unordered_map<ChunkPos, std::array<uint64_t, kSectionCount>> latest_;
    std::vector<ChunkManager::MeshWork> work_;
};

} // namespace mc
