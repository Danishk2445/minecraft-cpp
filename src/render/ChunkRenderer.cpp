#include "render/ChunkRenderer.h"

#include <algorithm>

#include "core/Log.h"
#include "gfx/Frustum.h"
#include "gfx/GL.h"

namespace mc {
namespace {

constexpr uint32_t kPageVertices = 4u << 20; // 64 MB of 16-byte vertices per page

void streamUpload(unsigned& buffer, const void* data, size_t bytes) {
    if (!buffer) glCreateBuffers(1, &buffer);
    glNamedBufferData(buffer, GLsizeiptr(bytes), data, GL_STREAM_DRAW);
}

} // namespace

std::optional<uint32_t> RangeAllocator::allocate(uint32_t count) {
    for (auto it = free_.begin(); it != free_.end(); ++it) {
        if (it->second < count) continue;
        const uint32_t offset = it->first;
        const uint32_t remaining = it->second - count;
        free_.erase(it);
        if (remaining) free_.emplace(offset + count, remaining);
        used_ += count;
        return offset;
    }
    return std::nullopt;
}

void RangeAllocator::release(uint32_t offset, uint32_t count) {
    used_ -= count;
    auto [it, inserted] = free_.emplace(offset, count);
    // Merge with the following block.
    auto next = std::next(it);
    if (next != free_.end() && it->first + it->second == next->first) {
        it->second += next->second;
        free_.erase(next);
    }
    // Merge with the preceding block.
    if (it != free_.begin()) {
        auto prev = std::prev(it);
        if (prev->first + prev->second == it->first) {
            prev->second += it->second;
            free_.erase(it);
        }
    }
}

ChunkRenderer::ChunkRenderer() = default;

ChunkRenderer::~ChunkRenderer() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (nearestSampler_) glDeleteSamplers(1, &nearestSampler_);
    if (commandBuffer_) glDeleteBuffers(1, &commandBuffer_);
    if (offsetBuffer_) glDeleteBuffers(1, &offsetBuffer_);
}

bool ChunkRenderer::init() {
    if (!shader_.load("terrain.vert", "terrain.frag")) return false;
    glCreateVertexArrays(1, &vao_);
    // Binding 0: TerrainVertex.
    glEnableVertexArrayAttrib(vao_, 0);
    glVertexArrayAttribIFormat(vao_, 0, 3, GL_UNSIGNED_SHORT, offsetof(TerrainVertex, x));
    glEnableVertexArrayAttrib(vao_, 1);
    glVertexArrayAttribIFormat(vao_, 1, 2, GL_UNSIGNED_BYTE, offsetof(TerrainVertex, blockLight));
    glEnableVertexArrayAttrib(vao_, 2);
    glVertexArrayAttribFormat(vao_, 2, 2, GL_UNSIGNED_SHORT, GL_TRUE, offsetof(TerrainVertex, u));
    glEnableVertexArrayAttrib(vao_, 3);
    glVertexArrayAttribIFormat(vao_, 3, 4, GL_UNSIGNED_BYTE, offsetof(TerrainVertex, r));
    for (unsigned a = 0; a < 4; ++a) glVertexArrayAttribBinding(vao_, a, 0);
    // Binding 1: per-draw section origin relative to the camera.
    glEnableVertexArrayAttrib(vao_, 4);
    glVertexArrayAttribFormat(vao_, 4, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(vao_, 4, 1);
    glVertexArrayBindingDivisor(vao_, 1, 1);

    glCreateSamplers(1, &nearestSampler_);
    glSamplerParameteri(nearestSampler_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glSamplerParameteri(nearestSampler_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glSamplerParameteri(nearestSampler_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(nearestSampler_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    ensureIndexCapacity(16384);
    return true;
}

void ChunkRenderer::ensureIndexCapacity(uint32_t quads) {
    if (quads <= quadCapacity_) return;
    const uint32_t cap = std::max({quads, quadCapacity_ * 2, 16384u});
    std::vector<uint32_t> idx(size_t(cap) * 6);
    for (uint32_t q = 0; q < cap; ++q) {
        const uint32_t v = q * 4;
        uint32_t* o = &idx[size_t(q) * 6];
        o[0] = v;
        o[1] = v + 1;
        o[2] = v + 2;
        o[3] = v + 2;
        o[4] = v + 3;
        o[5] = v;
    }
    quadIndices_.create(idx.size() * sizeof(uint32_t), idx.data());
    quadCapacity_ = cap;
    glVertexArrayElementBuffer(vao_, quadIndices_.id());
}

ChunkRenderer::Alloc ChunkRenderer::allocate(uint32_t vertices) {
    for (size_t p = 0; p < pages_.size(); ++p)
        if (auto off = pages_[p]->alloc.allocate(vertices)) return {int(p), *off, vertices};
    auto page = std::make_unique<Page>();
    const uint32_t cap = std::max(kPageVertices, vertices);
    page->vbo.create(size_t(cap) * sizeof(TerrainVertex), nullptr, GL_DYNAMIC_STORAGE_BIT);
    page->alloc = RangeAllocator(cap);
    pages_.push_back(std::move(page));
    log::debug("chunk renderer: new vertex page #{} ({} MB)", pages_.size(), cap * sizeof(TerrainVertex) >> 20);
    const auto off = pages_.back()->alloc.allocate(vertices);
    return {int(pages_.size() - 1), *off, vertices};
}

void ChunkRenderer::release(Alloc& a) {
    if (a.page >= 0 && a.count) pages_[size_t(a.page)]->alloc.release(a.offset, a.count);
    a = Alloc{};
}

void ChunkRenderer::upload(ChunkPos chunk, const SectionMesh& mesh) {
    SectionGpu& sg = chunks_[chunk].sections[size_t(mesh.section)];
    for (int l = 0; l < kLayerCount; ++l) {
        release(sg.layers[size_t(l)]);
        const auto& verts = mesh.layers[size_t(l)];
        if (verts.empty()) continue;
        Alloc a = allocate(uint32_t(verts.size()));
        pages_[size_t(a.page)]->vbo.update(size_t(a.offset) * sizeof(TerrainVertex), verts.size() * sizeof(TerrainVertex),
                                           verts.data());
        sg.layers[size_t(l)] = a;
        ensureIndexCapacity(uint32_t(verts.size() / 4));
    }
}

void ChunkRenderer::dropChunk(ChunkPos chunk) {
    auto it = chunks_.find(chunk);
    if (it == chunks_.end()) return;
    for (auto& s : it->second.sections)
        for (auto& a : s.layers) release(a);
    chunks_.erase(it);
}

void ChunkRenderer::clear() {
    std::vector<ChunkPos> all;
    for (const auto& [p, c] : chunks_) all.push_back(p);
    for (const auto& p : all) dropChunk(p);
}

size_t ChunkRenderer::gpuBytes() const {
    size_t b = 0;
    for (const auto& p : pages_) b += p->vbo.size();
    return b;
}

size_t ChunkRenderer::sectionCount() const {
    size_t n = 0;
    for (const auto& [p, c] : chunks_)
        for (const auto& s : c.sections)
            for (const auto& a : s.layers)
                if (a.count) {
                    ++n;
                    break;
                }
    return n;
}

void ChunkRenderer::render(const Camera& camera, const glm::mat4& viewProj, const Texture& atlas, int renderDistance) {
    stats_ = {};
    const Frustum frustum = Frustum::fromMatrix(viewProj);
    const double maxDist = renderDistance * 16.0 + 16.0;

    // Visible sections, computed once for all layers.
    struct Visible {
        const SectionGpu* section;
        glm::vec3 origin;
    };
    std::vector<Visible> visible;
    for (const auto& [pos, cg] : chunks_) {
        const double cx = pos.minBlockX() + 8.0 - camera.position.x, cz = pos.minBlockZ() + 8.0 - camera.position.z;
        if (cx * cx + cz * cz > maxDist * maxDist) continue;
        for (int si = 0; si < kSectionCount; ++si) {
            const SectionGpu& sg = cg.sections[size_t(si)];
            bool any = false;
            for (const auto& a : sg.layers) any |= a.count > 0;
            if (!any) continue;
            ++stats_.sections;
            const glm::vec3 origin(float(pos.minBlockX() - camera.position.x), float(sectionMinY(si) - camera.position.y),
                                   float(pos.minBlockZ() - camera.position.z));
            if (!frustum.intersects(origin - glm::vec3(1.0f), origin + glm::vec3(17.0f))) {
                ++stats_.culled;
                continue;
            }
            ++stats_.drawn;
            visible.push_back({&sg, origin});
        }
    }

    shader_.use();
    shader_.set("uViewProj", viewProj);
    atlas.bind(0);
    glBindVertexArray(vao_);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    for (int layer = 0; layer < kLayerCount; ++layer) {
        const RenderLayer rl = RenderLayer(layer);
        float cutoff = -1.0f;
        if (rl == RenderLayer::CutoutMipped) cutoff = 0.5f;
        if (rl == RenderLayer::Cutout) cutoff = 0.1f;
        shader_.set("uAlphaCutoff", cutoff);
        glBindSampler(0, rl == RenderLayer::Cutout ? nearestSampler_ : 0);
        if (rl == RenderLayer::Translucent) {
            glEnable(GL_BLEND);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
        } else {
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
        }
        for (size_t page = 0; page < pages_.size(); ++page) {
            commands_.clear();
            offsets_.clear();
            for (const Visible& v : visible) {
                const Alloc& a = v.section->layers[size_t(layer)];
                if (a.page != int(page) || a.count == 0) continue;
                commands_.push_back({a.count / 4 * 6, 1, 0, int32_t(a.offset), uint32_t(offsets_.size())});
                offsets_.push_back(v.origin);
                stats_.vertices += a.count;
            }
            if (commands_.empty()) continue;
            streamUpload(commandBuffer_, commands_.data(), commands_.size() * sizeof(DrawCommand));
            streamUpload(offsetBuffer_, offsets_.data(), offsets_.size() * sizeof(glm::vec3));
            glVertexArrayVertexBuffer(vao_, 0, pages_[page]->vbo.id(), 0, sizeof(TerrainVertex));
            glVertexArrayVertexBuffer(vao_, 1, offsetBuffer_, 0, sizeof(glm::vec3));
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, commandBuffer_);
            glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr, GLsizei(commands_.size()), 0);
            stats_.draws += int(commands_.size());
        }
    }
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
    glBindSampler(0, 0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

} // namespace mc
