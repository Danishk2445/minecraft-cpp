// ChunkRenderer.h -- section meshes in paged GPU vertex arenas, drawn per layer
// with glMultiDrawElementsIndirect. The camera-relative section origin of each
// draw comes from an instanced attribute selected through baseInstance.
#pragma once

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "core/Math.h"
#include "gfx/Camera.h"
#include "gfx/Shader.h"
#include "gfx/Texture.h"
#include "world/chunk/Chunk.h"
#include "world/mesh/ChunkMesher.h"

namespace mc {

// First-fit free-list over a fixed range of units (vertices).
class RangeAllocator {
public:
    explicit RangeAllocator(uint32_t capacity) : capacity_(capacity) { free_.emplace(0u, capacity); }
    std::optional<uint32_t> allocate(uint32_t count);
    void release(uint32_t offset, uint32_t count);
    uint32_t used() const { return used_; }
    uint32_t capacity() const { return capacity_; }

private:
    std::map<uint32_t, uint32_t> free_; // offset -> length
    uint32_t capacity_;
    uint32_t used_ = 0;
};

class ChunkRenderer {
public:
    ChunkRenderer();
    ~ChunkRenderer();
    bool init();

    // Replaces (or clears, if empty) the mesh of one section.
    void upload(ChunkPos chunk, const SectionMesh& mesh);
    void dropChunk(ChunkPos chunk);
    void clear();
    // Re-sorts translucent quads of sections within `radius` chunks once the camera
    // has moved more than a block since the last sort.
    void resortTranslucent(const glm::dvec3& camera, int radius);

    struct FrameStats {
        int sections = 0, drawn = 0, culled = 0, draws = 0;
        size_t vertices = 0;
    };
    struct Fog {
        glm::vec4 color{1.0f};
        glm::vec4 distances{1e9f, 1e9f + 1, 1e9f, 1e9f + 1}; // envStart, envEnd, rdStart, rdEnd
    };
    // Draws the opaque layers (solid, cutout_mipped, cutout). The lightmap must be
    // bound to texture unit 1. `renderDistance` in chunks (cylindrical cutoff).
    void renderOpaque(const Camera& camera, const glm::mat4& viewProj, const Texture& atlas, int renderDistance,
                      const Fog& fog);
    // Draws the translucent layer (after the opaque layers and entities).
    void renderTranslucent(const Camera& camera, const glm::mat4& viewProj, const Texture& atlas, const Fog& fog);
    const FrameStats& stats() const { return stats_; }
    size_t gpuBytes() const;
    size_t sectionCount() const;

private:
    struct Alloc {
        int page = -1;
        uint32_t offset = 0;
        uint32_t count = 0; // vertices
    };
    struct SectionGpu {
        std::array<Alloc, kLayerCount> layers;
        // CPU copy of the translucent layer for re-sorting as the camera moves.
        std::vector<TerrainVertex> translucent;
        std::vector<glm::vec3> centers;
    };
    struct ChunkGpu {
        std::array<SectionGpu, kSectionCount> sections;
    };
    struct Page {
        GpuBuffer vbo;
        RangeAllocator alloc{0};
    };

    void collectVisible(const Camera& camera, const glm::mat4& viewProj, int renderDistance);
    void drawLayers(int firstLayer, int lastLayer, const glm::mat4& viewProj, const Texture& atlas, const Fog& fog);
    Alloc allocate(uint32_t vertices);
    void release(Alloc& a);
    void ensureIndexCapacity(uint32_t quads);

    std::vector<std::unique_ptr<Page>> pages_;
    std::unordered_map<ChunkPos, ChunkGpu> chunks_;
    GpuBuffer quadIndices_;
    uint32_t quadCapacity_ = 0;
    unsigned vao_ = 0;
    unsigned nearestSampler_ = 0;
    Shader shader_;
    FrameStats stats_;
    // Per-frame scratch.
    struct DrawCommand {
        uint32_t count, instanceCount, firstIndex;
        int32_t baseVertex;
        uint32_t baseInstance;
    };
    struct Visible {
        const SectionGpu* section;
        glm::vec3 origin;
        float distance2;
    };
    std::vector<Visible> visible_;
    std::vector<DrawCommand> commands_;
    std::vector<glm::vec3> offsets_;
    unsigned commandBuffer_ = 0, offsetBuffer_ = 0; // re-specified every draw (stream)
    glm::dvec3 lastSortPos_{1e30};
};

} // namespace mc
