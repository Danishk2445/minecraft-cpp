#include "render/WorldRenderer.h"

#include <algorithm>

#include "render/ClientAssets.h"
#include "world/level/Level.h"
#include "gfx/GL.h"

namespace mc {

WorldRenderer::WorldRenderer(const ClientAssets& assets, const BlockRegistry& registry, const BiomeRegistry* biomes)
    : assets_(assets), registry_(registry), biomes_(biomes) {
    options_.colors = assets.defaultBiomeColors();
    options_.biomes = biomes;
    const Atlas& atlas = assets.blockAtlas();
    auto sprite = [&](const char* id) {
        const SpriteInfo& s = atlas.sprite(std::string("minecraft:block/") + id);
        return FluidSprites::Sprite{s.u0, s.v0, s.u1, s.v1};
    };
    options_.fluids.waterStill = sprite("water_still");
    options_.fluids.waterFlow = sprite("water_flow");
    options_.fluids.waterOverlay = sprite("water_overlay");
    options_.fluids.lavaStill = sprite("lava_still");
    options_.fluids.lavaFlow = sprite("lava_flow");
}

WorldRenderer::~WorldRenderer() = default;

bool WorldRenderer::init() {
    if (!particles_.init()) return false;
    if (!chunks_.init()) return false;
    if (!sky_.init(assets_.pack())) return false;
    lightmap_.init();
    return true;
}

void WorldRenderer::tick() { flicker_.tick(); }

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
        MesherOptions opts = options_;
        opts.cameraPos = glm::vec3(cameraPos_);
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

void WorldRenderer::render(const Camera& camera, glm::ivec2 viewport, int renderDistance, const Level& level,
                           float partialTick) {
    const float aspect = float(viewport.x) / float(std::max(viewport.y, 1));
    cameraPos_ = camera.position;
    chunks_.resortTranslucent(camera.position, 3);
    EnvironmentInputs in;
    in.dayTime = level.dayTime();
    in.partialTick = partialTick;
    in.lookDir = camera.forward();
    in.renderDistance = renderDistance;
    env_ = computeEnvironment(in);
    lightmap_.update(env_, gamma, flicker_.value());

    glViewport(0, 0, viewport.x, viewport.y);
    glClearColor(env_.fogColor.r, env_.fogColor.g, env_.fogColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    sky_.renderSky(camera, aspect, env_);

    const glm::mat4 viewProj = camera.projection(aspect) * camera.view();
    ChunkRenderer::Fog fog;
    fog.color = glm::vec4(env_.fogColor, 1.0f);
    fog.distances = glm::vec4(env_.fogEnvStart, env_.fogEnvEnd, env_.fogRdStart, env_.fogRdEnd);
    lightmap_.bind(1);
    chunks_.renderOpaque(camera, viewProj, assets_.blockAtlasTexture(), renderDistance, fog);
    chunks_.renderTranslucent(camera, viewProj, assets_.blockAtlasTexture(), fog);
    sky_.renderClouds(camera, aspect, env_, double(level.gameTime()) + partialTick);
}

void WorldRenderer::renderParticles(const ParticleSystem& particles, const Level& level, const Camera& camera,
                                    glm::ivec2 viewport, float partialTick) {
    const float aspect = float(viewport.x) / float(std::max(viewport.y, 1));
    const glm::mat4 viewProj = camera.projection(aspect) * camera.view();
    const ChunkManager& chunks = level.chunks();
    auto lightAt = [&chunks](const BlockPos& p) {
        const Chunk* c = chunks.chunk(ChunkPos::of(p));
        const uint8_t l = c ? c->lightAt(p.x & 15, p.y, p.z & 15) : packLight(15, 0);
        return glm::vec2(float(l & 15), float(l >> 4));
    };
    lightmap_.bind(1);
    particles_.render(particles, lightAt, camera, viewProj, assets_.blockAtlas(), assets_.blockAtlasTexture(), partialTick);
}

} // namespace mc
