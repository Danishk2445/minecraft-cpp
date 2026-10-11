#include "render/WorldRenderer.h"

#include <algorithm>

#include "render/ClientAssets.h"

namespace mc {

WorldRenderer::WorldRenderer(const ClientAssets& assets, const BlockRegistry& registry)
    : assets_(assets), registry_(registry) {
    options_.colors = assets.defaultBiomeColors();
}

WorldRenderer::~WorldRenderer() = default;

bool WorldRenderer::init() { return chunks_.init(); }

void WorldRenderer::update(ChunkManager& chunkManager, JobSystem& jobs) {
    for (const ChunkPos& p : chunkManager.takeDropped()) {
        chunks_.dropChunk(p);
        latest_.erase(p);
    }

    const size_t budget = size_t(std::max(4, int(jobs.workerCount()) * 2 - inFlight_));
    work_.clear();
    chunkManager.takeMeshWork(work_, budget);
    for (auto& w : work_) {
        auto& seqs = latest_[w.pos];
        const uint64_t seq = nextSeq_++;
        for (int si = 0; si < kSectionCount; ++si)
            if (w.sections & (1u << si)) seqs[size_t(si)] = seq;
        ++inFlight_;
        std::weak_ptr<int> token = alive_;
        JobSystem* js = &jobs;
        const BlockRegistry* reg = &registry_;
        const MesherOptions opts = options_;
        const int priority = w.pos.chebyshev(chunkManager.center());
        jobs.submit(priority, [this, token, js, reg, opts, seq, w = std::move(w)]() mutable {
            auto meshes = std::make_shared<std::vector<SectionMesh>>();
            meshSections(*reg, opts, w.neighborhood, w.sections, *meshes);
            js->postMain([this, token, seq, pos = w.pos, meshes]() {
                if (token.expired()) return;
                --inFlight_;
                auto it = latest_.find(pos);
                if (it == latest_.end()) return; // chunk dropped meanwhile
                for (const SectionMesh& m : *meshes)
                    if (it->second[size_t(m.section)] == seq) chunks_.upload(pos, m);
            });
        });
    }
}

void WorldRenderer::render(const Camera& camera, glm::ivec2 viewport, int renderDistance) {
    const float aspect = float(viewport.x) / float(std::max(viewport.y, 1));
    const glm::mat4 viewProj = camera.projection(aspect) * camera.view();
    chunks_.render(camera, viewProj, assets_.blockAtlasTexture(), renderDistance);
}

} // namespace mc
