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

    struct FrameStats {
        int sections = 0, drawn = 0, culled = 0, draws = 0;
        size_t vertices = 0;
    };
    // Draws all layers. `renderDistance` in chunks (cylindrical cutoff).
    void render(const Camera& camera, const glm::mat4& viewProj, const Texture& atlas, int renderDistance);
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
    };
    struct ChunkGpu {
        std::array<SectionGpu, kSectionCount> sections;
    };
    struct Page {
        GpuBuffer vbo;
        RangeAllocator alloc{0};
    };

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
    std::vector<DrawCommand> commands_;
    std::vector<glm::vec3> offsets_;
    unsigned commandBuffer_ = 0, offsetBuffer_ = 0; // re-specified every draw (stream)
};

} // namespace mc
