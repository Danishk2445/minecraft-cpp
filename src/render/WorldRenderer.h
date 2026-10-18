// WorldRenderer.h -- turns mesh-ready chunks into GPU meshes and draws the world.
#pragma once

#include "game/Options.h"

#include <array>
#include <memory>
#include <unordered_map>

#include "core/JobSystem.h"
#include "gfx/Camera.h"
#include "render/ChunkRenderer.h"
#include "render/EntityRenderer.h"
#include "render/FirstPersonRenderer.h"
#include "render/Environment.h"
#include "render/ParticleRenderer.h"
#include "render/SkyRenderer.h"
#include "render/WeatherRenderer.h"
#include "world/chunk/ChunkManager.h"
#include "world/mesh/BlockEntityMeshes.h"
#include "world/mesh/ChunkMesher.h"

namespace mc {

class BiomeRegistry;
class ClientAssets;
class Level;
class ParticleSystem;
class Player;

class WorldRenderer {
public:
    WorldRenderer(const ClientAssets& assets, const BlockRegistry& registry, const BiomeRegistry* biomes = nullptr);
    ~WorldRenderer();
    bool init();

    // Main thread, once per frame: drops unloaded chunks and schedules mesh jobs.
    void update(ChunkManager& chunks, JobSystem& jobs);
    void tick(); // 20 Hz: light flicker
    // Clears to the fog color and draws sky, terrain, the entities submitted to
    // entities() since its begin(), and clouds.
    void render(const Camera& camera, glm::ivec2 viewport, int renderDistance, const Level& level, float partialTick);
    EntityRenderer& entities() { return entities_; }
    // First-person hands over the finished world (call after render()), swayed by `view`.
    void renderHands(const Player& player, const Level& level, glm::ivec2 viewport, float partialTick,
                     const glm::mat4& view = glm::mat4(1.0f));
    void tickHands(const Player& player) { hands_.tick(player); }
    const EnvironmentState& environment() const { return env_; }
    // Particles over the world just drawn (uses its lightmap and camera).
    void renderParticles(const ParticleSystem& particles, const Level& level, const Camera& camera, glm::ivec2 viewport,
                         float partialTick);
    // Rain and snow (after particles, as vanilla's weather pass).
    void renderWeather(const Level& level, const Camera& camera, glm::ivec2 viewport, int64_t ticks, float partialTick);
    float gamma = 0.5f; // the Brightness option (0 = Moody, 1 = Bright)
    CloudMode clouds = CloudMode::Fancy;
    bool caveCulling = true; // vanilla smartCull (--cave-culling off for comparisons)
    bool fancyGraphics = true; // Fast: rain and snow within 5 blocks instead of 10
    // The Mipmap Levels option: the block atlas samples levels 0..n.
    void setMipmapLevels(int levels);
    // What the camera sees beyond the level itself this frame: lightning's flash, the
    // fluid around the camera and the eyes' adjustment to water (set before render()).
    struct View {
        float skyFlash = 0.0f;
        EnvironmentInputs::CameraFluid fluid = EnvironmentInputs::CameraFluid::None;
        uint32_t waterFogColor = 0x050533;
        float waterVision = 0.0f;
        bool closerWaterFog = false;
        bool spectator = false;
    } view;

    // No mesh jobs running.
    bool idle() const { return inFlight_ == 0; }
    const ChunkRenderer& chunkRenderer() const { return chunks_; }
    MesherOptions& mesherOptions() { return options_; }
    // Block entities the entity renderer draws instead of the chunk meshes (opening chests);
    // their sections remesh when the set changes.
    void setDynamicBlockEntities(std::unordered_set<BlockPos> positions, ChunkManager& chunks);

private:
    const ClientAssets& assets_;
    const BlockRegistry& registry_;
    const BiomeRegistry* biomes_ = nullptr;
    ChunkRenderer chunks_;
    glm::dvec3 cameraPos_{0.0};
    SkyRenderer sky_;
    ParticleRenderer particles_;
    WeatherRenderer weather_;
    EntityRenderer entities_;
    FirstPersonRenderer hands_;
    Lightmap lightmap_;
    LightFlicker flicker_;
    EnvironmentState env_;
    MesherOptions options_;
    BlockEntityMeshes blockEntities_;
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);
    int inFlight_ = 0;
    int mipmapLevels_ = -1;
    uint64_t nextSeq_ = 1;
    // Latest submitted mesh sequence per chunk section; older results are discarded.
    std::unordered_map<ChunkPos, std::array<uint64_t, kSectionCount>> latest_;
    std::vector<ChunkManager::MeshWork> work_;
};

} // namespace mc
