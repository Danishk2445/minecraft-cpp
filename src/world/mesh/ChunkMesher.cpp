#include "world/mesh/ChunkMesher.h"

#include "world/mesh/BlockEntityMeshes.h"

#include <algorithm>
#include <cmath>

#include "resources/BlockModels.h"
#include "world/biome/Biome.h"

namespace mc {
namespace {

// Block states and light of a section plus a two-cell border (smooth lighting
// looks up to two cells beyond a flush face).
constexpr int kBorder = 2;
constexpr int kPad = 16 + 2 * kBorder;

struct Cells {
    std::array<StateId, kPad * kPad * kPad> states;
    std::array<uint8_t, kPad * kPad * kPad> light;

    static constexpr int idx(int x, int y, int z) {
        return ((y + kBorder) * kPad + (z + kBorder)) * kPad + (x + kBorder);
    }
    StateId at(int x, int y, int z) const { return states[size_t(idx(x, y, z))]; }
    StateId at(const glm::ivec3& p) const { return at(p.x, p.y, p.z); }
    uint8_t lightAt(int x, int y, int z) const { return light[size_t(idx(x, y, z))]; }
    uint8_t lightAt(const glm::ivec3& p) const { return lightAt(p.x, p.y, p.z); }

    void fill(const Neighborhood& n, int minY) {
        for (int z = -kBorder; z < 16 + kBorder; ++z)
            for (int x = -kBorder; x < 16 + kBorder; ++x) {
                const ColumnSnapshot* col = n.column(floorDiv(x, 16), floorDiv(z, 16));
                const int lx = x & 15, lz = z & 15;
                for (int y = -kBorder; y < 16 + kBorder; ++y) {
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

// In-plane axes of each face, in vanilla AdjacencyInfo order: the first axis
// supplies the fallback value when a corner is hidden behind both edges.
constexpr int kAxis1[6] = {0, 0, 1, 0, 1, 1}; // D U N S W E -> x x y x y y
constexpr int kAxis2[6] = {2, 2, 0, 1, 2, 2}; //              -> z z x y z z

glm::ivec3 unit(int axis, int sign) {
    glm::ivec3 v(0);
    v[axis] = sign;
    return v;
}

// Smoothly lit corners of one face of one block: [a][b] = negative/positive side
// of axis1/axis2.
struct FaceLight {
    float ao[2][2];
    float sky[2][2]; // light level * 16
    float blk[2][2];
};

class Lighter {
public:
    Lighter(const BlockRegistry& reg, const Cells& cells) : reg_(reg), cells_(cells) {}

    // (sky, block) of a cell as the renderer sees it: block light is at least the
    // state's own emission.
    void cellLight(const glm::ivec3& p, int& sky, int& blk) const {
        const uint8_t l = cells_.lightAt(p);
        sky = l >> 4;
        blk = std::max<int>(l & 15, reg_.info(cells_.at(p)).emission);
    }
    float aoFactor(const glm::ivec3& p) const { return (reg_.info(cells_.at(p)).flags & kAoShade) ? 0.2f : 1.0f; }
    bool opaque(const glm::ivec3& p) const { return (reg_.info(cells_.at(p)).flags & kOpaqueCube) != 0; }

    FaceLight compute(const glm::ivec3& pos, Direction d, bool flush) const {
        const glm::ivec3 n = normal(d);
        const glm::ivec3 b = flush ? pos + n : pos;
        const int a1 = kAxis1[int(d)], a2 = kAxis2[int(d)];

        int cSky, cBlk;
        cellLight((flush || !opaque(pos + n)) ? pos + n : pos, cSky, cBlk);
        const float cAo = aoFactor(flush ? b : pos);

        FaceLight out{};
        for (int ia = 0; ia < 2; ++ia)
            for (int ib = 0; ib < 2; ++ib) {
                const glm::ivec3 e1 = b + unit(a1, ia ? 1 : -1);
                const glm::ivec3 e2 = b + unit(a2, ib ? 1 : -1);
                const bool open1 = !opaque(e1 + n), open2 = !opaque(e2 + n);
                int s1, b1, s2, b2, sd, bd;
                cellLight(e1, s1, b1);
                cellLight(e2, s2, b2);
                float ad;
                if (!open1 && !open2) {
                    sd = s1;
                    bd = b1;
                    ad = aoFactor(e1);
                } else {
                    const glm::ivec3 diag = e1 + unit(a2, ib ? 1 : -1);
                    cellLight(diag, sd, bd);
                    ad = aoFactor(diag);
                }
                out.ao[ia][ib] = (aoFactor(e1) + aoFactor(e2) + ad + cAo) * 0.25f;
                // Vanilla blend: a sample with no light at all takes the center's value.
                auto pick = [&](int s, int bl, int& os, int& ob) {
                    if (s == 0 && bl == 0) {
                        os = cSky;
                        ob = cBlk;
                    } else {
                        os = s;
                        ob = bl;
                    }
                };
                int ps1, pb1, ps2, pb2, psd, pbd;
                pick(s1, b1, ps1, pb1);
                pick(s2, b2, ps2, pb2);
                pick(sd, bd, psd, pbd);
                out.sky[ia][ib] = float(ps1 + ps2 + psd + cSky) * 4.0f; // average * 16
                out.blk[ia][ib] = float(pb1 + pb2 + pbd + cBlk) * 4.0f;
            }
        return out;
    }

private:
    const BlockRegistry& reg_;
    const Cells& cells_;
};


// Biome colors per block (cached per section), blended over a square like vanilla.
class BiomeTints {
public:
    BiomeTints(const MesherOptions& o, const Neighborhood& n) : opts_(o), n_(n) {}
    void reset(int minY) {
        minY_ = minY;
        valid_.fill(false);
    }
    const BiomeColors& at(int x, int y, int z) {
        if (!opts_.biomes) return opts_.colors;
        const size_t key = size_t(((y >> 2) * 16 + z) * 16 + x);
        if (!valid_[key]) {
            cache_[key] = compute(x, y, z);
            valid_[key] = true;
        }
        return cache_[key];
    }

private:
    BiomeColors compute(int x, int y, int z) const {
        const int r = std::max(0, opts_.biomeBlend);
        const int wy = minY_ + y;
        const int si = sectionIndexOfY(wy);
        const int qy = (y & 15) >> 2;
        uint32_t sum[4][3] = {};
        int count = 0;
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx) {
                const int lx = x + dx, lz = z + dz;
                const ColumnSnapshot* col = n_.column(floorDiv(lx, 16), floorDiv(lz, 16));
                if (!col) continue;
                const BiomeId id = col->biomes[size_t(si)][size_t((qy << 4) | (((lz & 15) >> 2) << 2) | ((lx & 15) >> 2))];
                const BiomeColors c =
                    opts_.biomes->colors(id, n_.center.minBlockX() + lx, n_.center.minBlockZ() + lz);
                const uint32_t v[4] = {c.grass, c.foliage, c.water, c.dryFoliage};
                for (int k = 0; k < 4; ++k) {
                    sum[k][0] += (v[k] >> 16) & 255;
                    sum[k][1] += (v[k] >> 8) & 255;
                    sum[k][2] += v[k] & 255;
                }
                ++count;
            }
        if (!count) return opts_.colors;
        auto avg = [&](int k) {
            return ((sum[k][0] / uint32_t(count)) << 16) | ((sum[k][1] / uint32_t(count)) << 8) | (sum[k][2] / uint32_t(count));
        };
        BiomeColors out;
        out.grass = avg(0);
        out.foliage = avg(1);
        out.water = avg(2);
        out.dryFoliage = avg(3);
        return out;
    }

    const MesherOptions& opts_;
    const Neighborhood& n_;
    int minY_ = 0;
    std::array<BiomeColors, 16 * 16 * 4> cache_;
    std::array<bool, 16 * 16 * 4> valid_{};
};

// Vanilla LiquidBlockRenderer.
class FluidMesher {
public:
    FluidMesher(const BlockRegistry& reg, const Cells& cells, const MesherOptions& o) : reg_(reg), cells_(cells), opts_(o) {}

    void mesh(const glm::ivec3& pos, const StateInfo& info, uint32_t waterColor, std::vector<TerrainVertex>& out,
              std::vector<glm::vec3>* centers) const {
        const FluidKind kind = info.fluid.kind;
        const bool lava = kind == FluidKind::Lava;
        const FluidSprites::Sprite& still = lava ? opts_.fluids.lavaStill : opts_.fluids.waterStill;
        const FluidSprites::Sprite& flow = lava ? opts_.fluids.lavaFlow : opts_.fluids.waterFlow;
        const glm::vec3 tint = lava ? glm::vec3(255.0f)
                                    : glm::vec3(float((waterColor >> 16) & 255), float((waterColor >> 8) & 255),
                                                float(waterColor & 255));

        auto same = [&](const glm::ivec3& p) { return fluidOf(p).kind == kind; };
        const bool above = same(pos + glm::ivec3(0, 1, 0));
        const float own = height(pos, kind);
        float ne, nw, se, sw;
        if (own >= 1.0f) {
            ne = nw = se = sw = 1.0f;
        } else {
            const float hn = height(pos + glm::ivec3(0, 0, -1), kind), hs = height(pos + glm::ivec3(0, 0, 1), kind);
            const float he = height(pos + glm::ivec3(1, 0, 0), kind), hw = height(pos + glm::ivec3(-1, 0, 0), kind);
            ne = average(own, hn, he, pos + glm::ivec3(1, 0, -1), kind);
            nw = average(own, hn, hw, pos + glm::ivec3(-1, 0, -1), kind);
            se = average(own, hs, he, pos + glm::ivec3(1, 0, 1), kind);
            sw = average(own, hs, hw, pos + glm::ivec3(-1, 0, 1), kind);
        }
        const glm::vec3 base(pos);

        // Top.
        if (!above && reg_.info(cells_.at(pos + glm::ivec3(0, 1, 0))).coverage[size_t(Direction::Down)] != 0xFFFF) {
            ne -= 0.001f, nw -= 0.001f, se -= 0.001f, sw -= 0.001f;
            const glm::vec2 f = flowVector(pos, info.fluid, kind);
            glm::vec2 uv[4];
            if (f.x == 0.0f && f.y == 0.0f) {
                uv[0] = {still.u(0), still.v(0)};
                uv[1] = {still.u(0), still.v(1)};
                uv[2] = {still.u(1), still.v(1)};
                uv[3] = {still.u(1), still.v(0)};
            } else {
                const float ag = std::atan2(f.y, f.x) - 1.5707964f;
                const float ah = std::sin(ag) * 0.25f, ai = std::cos(ag) * 0.25f;
                uv[0] = {flow.u(0.5f + (-ai - ah)), flow.v(0.5f + (-ai + ah))};
                uv[1] = {flow.u(0.5f + (-ai + ah)), flow.v(0.5f + (ai + ah))};
                uv[2] = {flow.u(0.5f + (ai + ah)), flow.v(0.5f + (ai - ah))};
                uv[3] = {flow.u(0.5f + (ai - ah)), flow.v(0.5f + (-ai - ah))};
            }
            const uint8_t light = lightAbove(pos);
            const glm::vec3 p[4] = {base + glm::vec3(0, nw, 0), base + glm::vec3(0, sw, 1), base + glm::vec3(1, se, 1),
                                    base + glm::vec3(1, ne, 0)};
            emit(out, centers, p, uv, tint * 1.0f, light);
            if (!blocksAbove(pos)) {
                const glm::vec3 q[4] = {p[0], p[3], p[2], p[1]};
                const glm::vec2 quv[4] = {uv[0], uv[3], uv[2], uv[1]};
                emit(out, centers, q, quv, tint * 1.0f, light);
            }
        }
        // Bottom.
        const glm::ivec3 below = pos + glm::ivec3(0, -1, 0);
        if (!same(below) && reg_.info(cells_.at(below)).coverage[size_t(Direction::Up)] != 0xFFFF) {
            const glm::vec3 p[4] = {base + glm::vec3(0, 0.001f, 1), base + glm::vec3(0, 0.001f, 0),
                                    base + glm::vec3(1, 0.001f, 0), base + glm::vec3(1, 0.001f, 1)};
            const glm::vec2 uv[4] = {{still.u(0), still.v(1)}, {still.u(0), still.v(0)}, {still.u(1), still.v(0)},
                                     {still.u(1), still.v(1)}};
            emit(out, centers, p, uv, tint * 0.5f, lightAbove(below));
        }
        // Sides.
        for (Direction d : kHorizontal) {
            const glm::ivec3 np = pos + normal(d);
            if (same(np)) continue;
            const StateInfo& ni = reg_.info(cells_.at(np));
            if (ni.coverage[size_t(opposite(d))] == 0xFFFF) continue;
            float h0, h1;
            glm::vec3 a, b;
            switch (d) {
            case Direction::North: h0 = nw, h1 = ne, a = {0, 0, 0.001f}, b = {1, 0, 0.001f}; break;
            case Direction::South: h0 = se, h1 = sw, a = {1, 0, 0.999f}, b = {0, 0, 0.999f}; break;
            case Direction::West: h0 = sw, h1 = nw, a = {0.001f, 0, 1}, b = {0.001f, 0, 0}; break;
            default: h0 = ne, h1 = se, a = {0.999f, 0, 0}, b = {0.999f, 0, 1}; break;
            }
            const bool overlay = !lava && ((ni.flags & kSkipSame) || ni.layer == RenderLayer::CutoutMipped) &&
                                 ni.coverage[size_t(opposite(d))] != 0 && (ni.flags & kHasGeometry);
            const FluidSprites::Sprite& sp = overlay ? opts_.fluids.waterOverlay : flow;
            const glm::vec2 uv[4] = {{sp.u(0.0f), sp.v((1.0f - h0) * 0.5f)}, {sp.u(0.5f), sp.v((1.0f - h1) * 0.5f)},
                                     {sp.u(0.5f), sp.v(0.5f)}, {sp.u(0.0f), sp.v(0.5f)}};
            const glm::vec3 p[4] = {base + a + glm::vec3(0, h0, 0), base + b + glm::vec3(0, h1, 0), base + b, base + a};
            const float shade = axisOf(d) == Axis::Z ? 0.8f : 0.6f;
            const uint8_t light = lightAbove(np);
            emit(out, centers, p, uv, tint * shade, light);
            if (!overlay) {
                const glm::vec3 q[4] = {p[0], p[3], p[2], p[1]};
                const glm::vec2 quv[4] = {uv[0], uv[3], uv[2], uv[1]};
                emit(out, centers, q, quv, tint * shade, light);
            }
        }
    }

private:
    FluidState fluidOf(const glm::ivec3& p) const { return reg_.info(cells_.at(p)).fluid; }
    static float ownHeight(const FluidState& f) {
        if (f.kind == FluidKind::None) return 0.0f;
        const int amount = f.falling ? 8 : 8 - f.level;
        return float(amount) / 9.0f;
    }
    float height(const glm::ivec3& p, FluidKind kind) const {
        const FluidState f = fluidOf(p);
        if (f.kind == kind) return fluidOf(p + glm::ivec3(0, 1, 0)).kind == kind ? 1.0f : ownHeight(f);
        return (reg_.info(cells_.at(p)).flags & kOpaqueCube) ? -1.0f : 0.0f;
    }
    float average(float own, float a, float b, const glm::ivec3& diag, FluidKind kind) const {
        if (a >= 1.0f || b >= 1.0f) return 1.0f;
        float sum = 0, weight = 0;
        auto add = [&](float h) {
            if (h >= 0.8f) sum += h * 10.0f, weight += 10.0f;
            else if (h >= 0.0f) sum += h, weight += 1.0f;
        };
        if (a > 0.0f || b > 0.0f) {
            const float d = height(diag, kind);
            if (d >= 1.0f) return 1.0f;
            add(d);
        }
        add(own);
        add(a);
        add(b);
        return weight > 0 ? sum / weight : own;
    }
    glm::vec2 flowVector(const glm::ivec3& pos, const FluidState& self, FluidKind kind) const {
        const float ownH = ownHeight(self);
        glm::vec2 f(0.0f);
        for (Direction d : kHorizontal) {
            const glm::ivec3 np = pos + normal(d);
            const FluidState nf = fluidOf(np);
            if (nf.kind != FluidKind::None && nf.kind != kind) continue;
            float h = ownHeight(nf), g = 0.0f;
            if (h == 0.0f) {
                if (!(reg_.info(cells_.at(np)).flags & kOpaqueCube)) {
                    const FluidState below = fluidOf(np + glm::ivec3(0, -1, 0));
                    if (below.kind == kind) {
                        h = ownHeight(below);
                        if (h > 0.0f) g = ownH - (h - 0.8888889f);
                    }
                }
            } else {
                g = ownH - h;
            }
            if (g != 0.0f) f += glm::vec2(float(normal(d).x), float(normal(d).z)) * g;
        }
        const float len = glm::length(f);
        return len > 1e-6f ? f / len : glm::vec2(0.0f);
    }
    bool blocksAbove(const glm::ivec3& pos) const {
        return (reg_.info(cells_.at(pos + glm::ivec3(0, 1, 0))).flags & kOpaqueCube) != 0;
    }
    // Vanilla fluid light: per-channel max of a cell and the cell above it.
    uint8_t lightAbove(const glm::ivec3& p) const {
        auto get = [&](const glm::ivec3& c) {
            const uint8_t l = cells_.lightAt(c);
            return std::pair<int, int>{l >> 4, std::max<int>(l & 15, reg_.info(cells_.at(c)).emission)};
        };
        const auto a = get(p), b = get(p + glm::ivec3(0, 1, 0));
        return packLight(std::max(a.first, b.first), std::max(a.second, b.second));
    }
    static void emit(std::vector<TerrainVertex>& out, std::vector<glm::vec3>* centers, const glm::vec3 (&p)[4],
                     const glm::vec2 (&uv)[4], const glm::vec3& color, uint8_t light) {
        const uint8_t sky = uint8_t((light >> 4) * 16), blk = uint8_t((light & 15) * 16);
        for (int i = 0; i < 4; ++i)
            out.push_back({packPos(p[i].x), packPos(p[i].y), packPos(p[i].z), blk, sky, packUV(uv[i].x), packUV(uv[i].y),
                           uint8_t(std::min(color.r + 0.5f, 255.0f)), uint8_t(std::min(color.g + 0.5f, 255.0f)),
                           uint8_t(std::min(color.b + 0.5f, 255.0f)), 0});
        if (centers) centers->push_back((p[0] + p[1] + p[2] + p[3]) * 0.25f);
    }

    const BlockRegistry& reg_;
    const Cells& cells_;
    const MesherOptions& opts_;
};

// Back-to-front order for a translucent layer (groups of 4 vertices).
void sortTranslucent(std::vector<TerrainVertex>& verts, std::vector<glm::vec3>& centers, const glm::vec3& eye) {
    const size_t quads = centers.size();
    if (quads < 2 || verts.size() != quads * 4) return;
    std::vector<uint32_t> order(quads);
    for (uint32_t i = 0; i < quads; ++i) order[i] = i;
    std::vector<float> dist(quads);
    for (size_t i = 0; i < quads; ++i) {
        const glm::vec3 d = centers[i] - eye;
        dist[i] = glm::dot(d, d);
    }
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return dist[a] > dist[b]; });
    std::vector<TerrainVertex> sorted(verts.size());
    std::vector<glm::vec3> sortedCenters(quads);
    for (size_t i = 0; i < quads; ++i) {
        std::copy_n(&verts[size_t(order[i]) * 4], 4, &sorted[i * 4]);
        sortedCenters[i] = centers[order[i]];
    }
    verts.swap(sorted);
    centers.swap(sortedCenters);
}

} // namespace

void sortTranslucentQuads(std::vector<TerrainVertex>& vertices, std::vector<glm::vec3>& centers, const glm::vec3& eye) {
    sortTranslucent(vertices, centers, eye);
}

void meshSections(const BlockRegistry& reg, const MesherOptions& options, const Neighborhood& n, uint32_t sectionMask,
                  std::vector<SectionMesh>& out, MesherStats* stats) {
    Cells cells;
    const Lighter lighter(reg, cells);
    BiomeTints tints(options, n);
    const FluidMesher fluids(reg, cells, options);
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
        tints.reset(minY);
        const int wx0 = n.center.minBlockX(), wz0 = n.center.minBlockZ();

        for (int y = 0; y < 16; ++y)
            for (int z = 0; z < 16; ++z)
                for (int x = 0; x < 16; ++x) {
                    const StateId s = cells.at(x, y, z);
                    const StateInfo& info = reg.info(s);
                    if (info.fluid.kind != FluidKind::None) {
                        const RenderLayer fl = info.fluid.kind == FluidKind::Lava ? RenderLayer::Solid : RenderLayer::Translucent;
                        fluids.mesh(glm::ivec3(x, y, z), info, tints.at(x, y, z).water, mesh.layers[size_t(fl)],
                                    fl == RenderLayer::Translucent ? &mesh.translucentCenters : nullptr);
                    }
                    if (options.blockEntities)
                        if (const auto* quads = options.blockEntities->quads(s)) {
                            // Block entities: flat light from their own cell, entity shading baked in.
                            int sky, blk;
                            lighter.cellLight(glm::ivec3(x, y, z), sky, blk);
                            auto& verts = mesh.layers[size_t(RenderLayer::Cutout)];
                            for (const EntityQuad& q : *quads) {
                                for (int k = 0; k < 4; ++k) {
                                    const glm::vec3 p = glm::vec3(x, y, z) + q.pos[size_t(k)];
                                    verts.push_back({packPos(p.x), packPos(p.y), packPos(p.z), uint8_t(blk * 16),
                                                     uint8_t(sky * 16), packUV(q.uv[size_t(k)].x), packUV(q.uv[size_t(k)].y),
                                                     uint8_t(std::min(q.color.r + 0.5f, 255.0f)),
                                                     uint8_t(std::min(q.color.g + 0.5f, 255.0f)),
                                                     uint8_t(std::min(q.color.b + 0.5f, 255.0f)), 0});
                                }
                                if (stats) ++stats->quads;
                            }
                            // Fall through: most block-entity models have no quads, the bell's frame does.
                        }
                    if ((info.flags & kAir) || !(info.flags & kHasGeometry) || info.layer == RenderLayer::Invisible ||
                        (info.flags & kCodeRendered))
                        continue;
                    if (stats) ++stats->blocks;
                    models.clear();
                    BlockModels::collect(reg.modelChoices(s), BlockPos(wx0 + x, minY + y, wz0 + z), models);

                    const Block& block = reg.blockOf(s);
                    int power = 0, age = 0;
                    if (block.tint.source == TintSource::Redstone) power = reg.intValue(s, "power");
                    if (block.tint.source == TintSource::Stem) age = reg.intValue(s, "age");
                    auto& verts = mesh.layers[size_t(info.layer)];
                    const glm::ivec3 pos(x, y, z);

                    // Lazily computed smooth light per (face, flush).
                    FaceLight faceLight[12];
                    bool haveFace[12] = {};

                    for (const BakedModel* m : models) {
                        const bool smooth = options.smoothLighting && m->ambientOcclusion && info.emission == 0;
                        for (const BakedQuad& q : m->quads) {
                            if (q.cull >= 0) {
                                const glm::ivec3 o = normal(Direction(q.cull));
                                const StateId nb = cells.at(pos + o);
                                const StateInfo& ni = reg.info(nb);
                                const bool hidden = (q.cullMask & ~ni.coverage[size_t(opposite(Direction(q.cull)))]) == 0;
                                const bool same = (info.flags & kSkipSame) && ni.block == info.block;
                                if (hidden || same) {
                                    if (stats) ++stats->culled;
                                    continue;
                                }
                            }
                            const uint32_t tint =
                                q.tint < 0 ? 0xFFFFFFu : resolveTint(block.tint, q.tint, tints.at(x, y, z), power, age);
                            const glm::vec3 tintColor(float((tint >> 16) & 255), float((tint >> 8) & 255), float(tint & 255));
                            const float shade = q.shade ? faceShade(q.face) : 1.0f;

                            float bright[4], skyL[4], blkL[4];
                            if (smooth) {
                                const int key = int(q.face) * 2 + (q.flush ? 1 : 0);
                                if (!haveFace[key]) {
                                    faceLight[key] = lighter.compute(pos, q.face, q.flush);
                                    haveFace[key] = true;
                                }
                                const FaceLight& fl = faceLight[key];
                                const int a1 = kAxis1[int(q.face)], a2 = kAxis2[int(q.face)];
                                for (int i = 0; i < 4; ++i) {
                                    const float t1 = std::clamp(q.pos[size_t(i)][a1], 0.0f, 1.0f);
                                    const float t2 = std::clamp(q.pos[size_t(i)][a2], 0.0f, 1.0f);
                                    const float w[2][2] = {{(1 - t1) * (1 - t2), (1 - t1) * t2}, {t1 * (1 - t2), t1 * t2}};
                                    float ao = 0, sl = 0, bl = 0;
                                    for (int ia = 0; ia < 2; ++ia)
                                        for (int ib = 0; ib < 2; ++ib) {
                                            ao += fl.ao[ia][ib] * w[ia][ib];
                                            sl += fl.sky[ia][ib] * w[ia][ib];
                                            bl += fl.blk[ia][ib] * w[ia][ib];
                                        }
                                    bright[i] = ao * shade;
                                    skyL[i] = sl;
                                    blkL[i] = std::max(bl, float(q.emission) * 16.0f);
                                }
                            } else {
                                // Flat: light of the cell the face looks into (flush) or the block's own.
                                int sky, blk;
                                lighter.cellLight(q.flush ? pos + normal(q.face) : pos, sky, blk);
                                blk = std::max<int>({blk, info.emission, q.emission});
                                for (int i = 0; i < 4; ++i) {
                                    bright[i] = shade;
                                    skyL[i] = float(sky * 16);
                                    blkL[i] = float(blk * 16);
                                }
                            }

                            // Split the quad along the diagonal joining its brighter corners
                            // (avoids the classic AO anisotropy seam).
                            int first = 0;
                            if (smooth) {
                                float b[4];
                                for (int i = 0; i < 4; ++i)
                                    b[i] = bright[i] * (0.2f + std::max(skyL[i], blkL[i]) / 240.0f);
                                if (b[0] + b[2] < b[1] + b[3]) first = 1;
                            }
                            for (int k = 0; k < 4; ++k) {
                                const int i = (k + first) & 3;
                                const glm::vec3 p = glm::vec3(pos) + q.pos[size_t(i)];
                                const glm::vec3 c = tintColor * bright[i];
                                verts.push_back({packPos(p.x), packPos(p.y), packPos(p.z),
                                                 uint8_t(std::clamp(blkL[i], 0.0f, 240.0f) + 0.5f),
                                                 uint8_t(std::clamp(skyL[i], 0.0f, 240.0f) + 0.5f),
                                                 packUV(q.uv[size_t(i)].x), packUV(q.uv[size_t(i)].y),
                                                 uint8_t(std::min(c.r + 0.5f, 255.0f)), uint8_t(std::min(c.g + 0.5f, 255.0f)),
                                                 uint8_t(std::min(c.b + 0.5f, 255.0f)), 0});
                            }
                            if (info.layer == RenderLayer::Translucent) {
                                glm::vec3 c(0.0f);
                                for (const auto& p : q.pos) c += p;
                                mesh.translucentCenters.push_back(glm::vec3(pos) + c * 0.25f);
                            }
                            if (stats) ++stats->quads;
                        }
                    }
                }
        const glm::vec3 eye = options.cameraPos - glm::vec3(float(wx0), float(minY), float(wz0));
        sortTranslucent(mesh.layers[size_t(RenderLayer::Translucent)], mesh.translucentCenters, eye);
        out.push_back(std::move(mesh));
    }
}

} // namespace mc
