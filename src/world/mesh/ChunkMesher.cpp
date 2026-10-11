#include "world/mesh/ChunkMesher.h"

#include <algorithm>
#include <cmath>

#include "resources/BlockModels.h"

namespace mc {
namespace {

constexpr int kPad = 18; // 16 + one cell border on each side

// Block states of a section plus a one-block border, gathered once per section.
struct Cells {
    std::array<StateId, kPad * kPad * kPad> states;
    std::array<uint8_t, kPad * kPad * kPad> light;

    static constexpr int idx(int x, int y, int z) { return ((y + 1) * kPad + (z + 1)) * kPad + (x + 1); }
    StateId at(int x, int y, int z) const { return states[size_t(idx(x, y, z))]; }
    uint8_t lightAt(int x, int y, int z) const { return light[size_t(idx(x, y, z))]; }

    void fill(const Neighborhood& n, int minY) {
        for (int z = -1; z <= 16; ++z)
            for (int x = -1; x <= 16; ++x) {
                const ColumnSnapshot* col = n.column(floorDiv(x, 16), floorDiv(z, 16));
                const int lx = x & 15, lz = z & 15;
                for (int y = -1; y <= 16; ++y) {
                    const size_t i = size_t(idx(x, y, z));
                    if (!col) {
                        states[i] = 0;
                        light[i] = packLight(15, 0);
                        continue;
                    }
                    states[i] = col->get(lx, minY + y, lz);
                    light[i] = col->lightAt(lx, minY + y, lz);
                }
            }
    }
};

float faceShade(Direction d) {
    switch (d) {
    case Direction::Down: return 0.5f;
    case Direction::Up: return 1.0f;
    case Direction::North:
    case Direction::South: return 0.8f;
    default: return 0.6f;
    }
}

uint16_t packPos(float v) { return uint16_t(std::clamp(int(std::lround((v + 8.0f) * 2048.0f)), 0, 65535)); }
uint16_t packUV(float v) { return uint16_t(std::clamp(int(std::lround(v * 65535.0f)), 0, 65535)); }

int layerIndex(RenderLayer l) { return int(l); }

} // namespace

void meshSections(const BlockRegistry& reg, const MesherOptions& options, const Neighborhood& n, uint32_t sectionMask,
                  std::vector<SectionMesh>& out, MesherStats* stats) {
    Cells cells;
    std::vector<const BakedModel*> models;
    const ColumnSnapshot* self = n.column(0, 0);
    for (int si = 0; si < kSectionCount; ++si) {
        if (!(sectionMask & (1u << si))) continue;
        SectionMesh mesh;
        mesh.section = si;
        if (!self || !self->blocks[size_t(si)]) {
            out.push_back(std::move(mesh));
            continue;
        }
        const int minY = sectionMinY(si);
        cells.fill(n, minY);
        const int wx0 = n.center.minBlockX(), wz0 = n.center.minBlockZ();

        for (int y = 0; y < 16; ++y)
            for (int z = 0; z < 16; ++z)
                for (int x = 0; x < 16; ++x) {
                    const StateId s = cells.at(x, y, z);
                    const StateInfo& info = reg.info(s);
                    if ((info.flags & kAir) || !(info.flags & kHasGeometry) || info.layer == RenderLayer::Invisible)
                        continue;
                    if (stats) ++stats->blocks;
                    models.clear();
                    BlockModels::collect(reg.modelChoices(s), BlockPos(wx0 + x, minY + y, wz0 + z), models);

                    const Block& block = reg.blockOf(s);
                    int power = 0, age = 0;
                    if (block.tint.source == TintSource::Redstone) power = reg.intValue(s, "power");
                    if (block.tint.source == TintSource::Stem) age = reg.intValue(s, "age");
                    auto& verts = mesh.layers[size_t(layerIndex(info.layer))];
                    const uint8_t light = cells.lightAt(x, y, z);

                    for (const BakedModel* m : models) {
                        for (const BakedQuad& q : m->quads) {
                            if (q.cull >= 0) {
                                const glm::ivec3 o = normal(Direction(q.cull));
                                const StateId nb = cells.at(x + o.x, y + o.y, z + o.z);
                                const StateInfo& ni = reg.info(nb);
                                const bool hidden = (q.cullMask & ~ni.coverage[size_t(opposite(Direction(q.cull)))]) == 0;
                                const bool same = (info.flags & kSkipSame) && reg.info(nb).block == info.block;
                                if (hidden || same) {
                                    if (stats) ++stats->culled;
                                    continue;
                                }
                            }
                            const uint32_t tint = resolveTint(block.tint, q.tint, options.colors, power, age);
                            const float shade = q.shade ? faceShade(q.face) : 1.0f;
                            const uint8_t r = uint8_t(float((tint >> 16) & 255) * shade + 0.5f);
                            const uint8_t g = uint8_t(float((tint >> 8) & 255) * shade + 0.5f);
                            const uint8_t b = uint8_t(float(tint & 255) * shade + 0.5f);
                            // Light: the cell the face looks into for flush quads, else the block's own.
                            uint8_t l = light;
                            if (q.flush) {
                                const glm::ivec3 o = normal(q.face);
                                l = cells.lightAt(x + o.x, y + o.y, z + o.z);
                            }
                            const uint8_t sky = uint8_t((l >> 4) * 16);
                            const uint8_t blk = uint8_t(std::max<int>(l & 15, q.emission) * 16);
                            for (int i = 0; i < 4; ++i) {
                                const glm::vec3 p = glm::vec3(float(x), float(y), float(z)) + q.pos[size_t(i)];
                                verts.push_back({packPos(p.x), packPos(p.y), packPos(p.z), blk, sky,
                                                 packUV(q.uv[size_t(i)].x), packUV(q.uv[size_t(i)].y), r, g, b, 0});
                            }
                            if (info.layer == RenderLayer::Translucent) {
                                glm::vec3 c(0.0f);
                                for (const auto& p : q.pos) c += p;
                                mesh.translucentCenters.push_back(glm::vec3(float(x), float(y), float(z)) + c * 0.25f);
                            }
                            if (stats) ++stats->quads;
                        }
                    }
                }
        (void)options.smoothLighting;
        out.push_back(std::move(mesh));
    }
}

} // namespace mc
