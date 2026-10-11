#include "world/mesh/ChunkMesher.h"

#include <algorithm>
#include <cmath>

#include "resources/BlockModels.h"

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

} // namespace

void meshSections(const BlockRegistry& reg, const MesherOptions& options, const Neighborhood& n, uint32_t sectionMask,
                  std::vector<SectionMesh>& out, MesherStats* stats) {
    Cells cells;
    const Lighter lighter(reg, cells);
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
                            const uint32_t tint = resolveTint(block.tint, q.tint, options.colors, power, age);
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
        out.push_back(std::move(mesh));
    }
}

} // namespace mc
