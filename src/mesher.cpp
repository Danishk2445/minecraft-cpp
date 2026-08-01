#include "mesher.h"
#include <cmath>
#include <cstring>
#include <vector>

namespace {

// Face order: 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z (matches BlockDef::tex).
const int FACE_NORMAL[6][3] = {
    {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
const float FACE_SHADE[6] = {0.65f, 0.65f, 1.0f, 0.55f, 0.82f, 0.82f};
const float AO_CURVE[4] = {0.5f, 0.7f, 0.85f, 1.0f};

// In-plane axes per normal axis, chosen so va is vertical on side faces
// (keeps side textures upright when UVs are taken from world coordinates).
const int UAXIS[3] = {2, 0, 0}; // n=x -> u along z ; n=y -> x ; n=z -> x
const int VAXIS[3] = {1, 2, 1}; // n=x -> v along y ; n=y -> z ; n=z -> y

// Quad corner offsets in (u,v) space and the emit order per face direction
// that yields CCW winding seen from outside.
const int CORNER[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
const int RECT_ORDER[6][4] = {
    {0, 3, 2, 1}, // +X
    {0, 1, 2, 3}, // -X
    {0, 3, 2, 1}, // +Y
    {0, 1, 2, 3}, // -Y
    {0, 1, 2, 3}, // +Z
    {0, 3, 2, 1}, // -Z
};

// Water still uses explicit per-face tables (no merging, surface lowering).
const int FACE_VERT[6][4][3] = {
    {{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}, // +X
    {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}, // -X
    {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}, // +Y
    {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}, // -Y
    {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}, // +Z
    {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}, // -Z
};
const float FACE_UV[6][4][2] = {
    {{0, 1}, {1, 1}, {1, 0}, {0, 0}},
    {{0, 1}, {1, 1}, {1, 0}, {0, 0}},
    {{0, 1}, {1, 1}, {1, 0}, {0, 0}},
    {{0, 0}, {1, 0}, {1, 1}, {0, 1}},
    {{0, 1}, {1, 1}, {1, 0}, {0, 0}},
    {{0, 1}, {1, 1}, {1, 0}, {0, 0}},
};

// Cached 3x3 chunk neighborhood: all queries during meshing stay within one
// chunk of the center, so avoid the hash map on every lookup.
struct Neigh {
    Chunk* c[3][3] = {};
    int bx = 0, bz = 0;

    const Chunk* at(int wx, int wz) const {
        return c[((wx - bx) >> 4) + 1][((wz - bz) >> 4) + 1];
    }
    uint8_t block(int wx, int wy, int wz) const {
        if (wy < 0) return B_BEDROCK;
        if (wy >= CY) return B_AIR;
        const Chunk* ch = at(wx, wz);
        if (!ch) return B_AIR;
        return ch->get(wx & 15, wy, wz & 15);
    }
    // High nibble sky, low nibble block light.
    uint8_t rawLight(int wx, int wy, int wz) const {
        if (wy >= CY) return 0xF0;
        if (wy < 0) return 0;
        const Chunk* ch = at(wx, wz);
        if (!ch) return 0;
        return ch->light[blockIndex(wx & 15, wy, wz & 15)];
    }
    bool occludes(int wx, int wy, int wz) const {
        uint8_t b = block(wx, wy, wz);
        return blockOpaque(b) || b == B_OAK_LEAVES;
    }
};

// Vertex layout: x,y,z, u,v, layer, shade, sky, block = 9 floats.
struct MeshData {
    std::vector<float> verts;
    std::vector<uint32_t> idx;

    void quad(const float p[4][3], const float uv[4][2], float layer,
              const float shade[4], const float sky[4], const float blk[4], bool flip) {
        uint32_t base = (uint32_t)(verts.size() / 9);
        for (int i = 0; i < 4; i++)
            verts.insert(verts.end(), {p[i][0], p[i][1], p[i][2], uv[i][0], uv[i][1],
                                       layer, shade[i], sky[i], blk[i]});
        if (flip) idx.insert(idx.end(), {base + 1, base + 2, base + 3, base + 3, base + 0, base + 1});
        else      idx.insert(idx.end(), {base + 0, base + 1, base + 2, base + 2, base + 3, base + 0});
    }
};

bool faceVisible(uint8_t self, uint8_t nb) {
    const BlockDef& S = g_blocks[self];
    const BlockDef& N = g_blocks[nb];
    if (S.fluid) return nb != self && !N.opaque;
    if (self == B_OAK_LEAVES) return !N.opaque; // leaves render between leaves
    if (S.cutout) return !N.opaque && nb != self;
    return !N.opaque;
}

// Per-face data stored in the greedy mask. sky4/blk4 are sums over the 4
// smoothing cells per corner (0..60).
struct FaceCell {
    uint16_t layer;
    uint8_t present;
    uint8_t fluid; // water surface: rendered in the translucent pass at y+0.875
    uint8_t ao[4];
    uint8_t sky4[4];
    uint8_t blk4[4];

    bool uniform() const {
        for (int i = 1; i < 4; i++)
            if (ao[i] != ao[0] || sky4[i] != sky4[0] || blk4[i] != blk4[0]) return false;
        return true;
    }
};

bool mergeable(const FaceCell& a, const FaceCell& b) {
    return a.present && b.present && a.layer == b.layer && a.fluid == b.fluid &&
           a.uniform() && b.uniform() &&
           a.ao[0] == b.ao[0] && a.sky4[0] == b.sky4[0] && a.blk4[0] == b.blk4[0];
}

// Sample AO + smooth light for the 4 corners of a face.
// cell = block position (world), q = the cell the face looks into.
void sampleFace(const Neigh& n, int dir, const int q[3], FaceCell& fc) {
    int nAx = dir / 2;
    int ua = UAXIS[nAx], va = VAXIS[nAx];
    uint8_t rawQ = n.rawLight(q[0], q[1], q[2]);
    for (int ci = 0; ci < 4; ci++) {
        int du = CORNER[ci][0] ? 1 : -1;
        int dv = CORNER[ci][1] ? 1 : -1;
        int c1[3] = {q[0], q[1], q[2]}; c1[ua] += du;
        int c2[3] = {q[0], q[1], q[2]}; c2[va] += dv;
        int c3[3] = {q[0], q[1], q[2]}; c3[ua] += du; c3[va] += dv;
        bool o1 = n.occludes(c1[0], c1[1], c1[2]);
        bool o2 = n.occludes(c2[0], c2[1], c2[2]);
        bool o3 = n.occludes(c3[0], c3[1], c3[2]);
        fc.ao[ci] = (o1 && o2) ? 0 : (uint8_t)(3 - (o1 + o2 + o3));

        int skySum = rawQ >> 4, blkSum = rawQ & 15;
        const int* cells[3] = {c1, c2, c3};
        for (const int* cc : cells) {
            uint8_t raw = blockOpaque(n.block(cc[0], cc[1], cc[2]))
                              ? rawQ : n.rawLight(cc[0], cc[1], cc[2]);
            skySum += raw >> 4;
            blkSum += raw & 15;
        }
        fc.sky4[ci] = (uint8_t)skySum;
        fc.blk4[ci] = (uint8_t)blkSum;
    }
}

void emitRect(MeshData& m, int dir, int slice, int u0, int v0, int w, int h,
              const FaceCell& fc) {
    int nAx = dir / 2;
    int ua = UAXIS[nAx], va = VAXIS[nAx];
    bool positive = (dir % 2) == 0;
    float plane = (float)slice + (positive ? 1.0f : 0.0f);
    if (fc.fluid) plane = (float)slice + 0.875f; // lowered water surface

    float p[4][3], uv[4][2], shade[4], sky[4], blk[4];
    const int* order = RECT_ORDER[dir];
    for (int k = 0; k < 4; k++) {
        int ci = order[k];
        float cu = (float)(u0 + CORNER[ci][0] * w);
        float cv = (float)(v0 + CORNER[ci][1] * h);
        p[k][nAx] = plane;
        p[k][ua] = cu;
        p[k][va] = cv;
        if (nAx == 1) { uv[k][0] = p[k][0]; uv[k][1] = p[k][2]; }
        else          { uv[k][0] = p[k][ua]; uv[k][1] = -p[k][1]; }
        shade[k] = FACE_SHADE[dir] * AO_CURVE[fc.ao[ci]];
        sky[k] = fc.sky4[ci] / 60.0f;
        blk[k] = fc.blk4[ci] / 60.0f;
    }
    bool flip = fc.ao[order[0]] + fc.ao[order[2]] < fc.ao[order[1]] + fc.ao[order[3]];
    m.quad(p, uv, fc.layer, shade, sky, blk, flip);
}

void crossPlant(MeshData& m, int x, int y, int z, uint16_t layer, float sky, float blk) {
    const float a = 0.15f, b = 0.85f;
    float q[2][4][3] = {
        {{x + a, (float)y, z + a}, {x + b, (float)y, z + b}, {x + b, y + 1.0f, z + b}, {x + a, y + 1.0f, z + a}},
        {{x + a, (float)y, z + b}, {x + b, (float)y, z + a}, {x + b, y + 1.0f, z + a}, {x + a, y + 1.0f, z + b}},
    };
    const float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    float sh[4] = {1, 1, 1, 1};
    float sk[4] = {sky, sky, sky, sky};
    float bl[4] = {blk, blk, blk, blk};
    for (int i = 0; i < 2; i++) {
        m.quad(q[i], uv, layer, sh, sk, bl, false);
        // back side: reversed winding so the plant is visible from both sides
        const float ruv[4][2] = {{1, 1}, {0, 1}, {0, 0}, {1, 0}};
        float rq[4][3] = {{q[i][1][0], q[i][1][1], q[i][1][2]},
                          {q[i][0][0], q[i][0][1], q[i][0][2]},
                          {q[i][3][0], q[i][3][1], q[i][3][2]},
                          {q[i][2][0], q[i][2][1], q[i][2][2]}};
        m.quad(rq, ruv, layer, sh, sk, bl, false);
    }
}

void uploadPart(GLuint& vao, GLuint& vbo, GLuint& ebo, GLsizei& count, const MeshData& m) {
    if (vao) { glDeleteVertexArrays(1, &vao); glDeleteBuffers(1, &vbo); glDeleteBuffers(1, &ebo); }
    vao = vbo = ebo = 0;
    count = (GLsizei)m.idx.size();
    if (!count) return;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, m.verts.size() * sizeof(float), m.verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m.idx.size() * sizeof(uint32_t), m.idx.data(), GL_STATIC_DRAW);
    GLsizei stride = 9 * sizeof(float);
    const int sizes[6] = {3, 2, 1, 1, 1, 1};
    size_t off = 0;
    for (int i = 0; i < 6; i++) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i, sizes[i], GL_FLOAT, GL_FALSE, stride, (void*)(off * sizeof(float)));
        off += sizes[i];
    }
    glBindVertexArray(0);
}

} // namespace

void buildChunkMesh(World& world, Chunk& chunk) {
    Neigh n;
    n.bx = chunk.cx * CX;
    n.bz = chunk.cz * CZ;
    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++)
            n.c[dx + 1][dz + 1] = world.chunk(chunk.cx + dx, chunk.cz + dz);

    MeshData opaque, water;

    // ---- pass 1: cross plants and water (simple per-face)
    for (int y = 0; y < CY; y++)
        for (int z = 0; z < CZ; z++)
            for (int x = 0; x < CX; x++) {
                uint8_t b = chunk.get(x, y, z);
                if (b == B_AIR) continue;
                const BlockDef& def = g_blocks[b];
                int wx = n.bx + x, wz = n.bz + z;

                if (def.cross) {
                    uint8_t raw = n.rawLight(wx, y, wz);
                    crossPlant(opaque, x, y, z, def.tex[0], (raw >> 4) / 15.0f, (raw & 15) / 15.0f);
                    continue;
                }
                if (!def.fluid) continue;

                bool topOpen = n.block(wx, y + 1, wz) != B_WATER;
                for (int f = 0; f < 6; f++) {
                    if (f == 2) continue; // +Y water surfaces go through the greedy pass
                    int qx = wx + FACE_NORMAL[f][0];
                    int qy = y + FACE_NORMAL[f][1];
                    int qz = wz + FACE_NORMAL[f][2];
                    if (!faceVisible(b, n.block(qx, qy, qz))) continue;
                    uint8_t raw = n.rawLight(qx, qy, qz);
                    float skyv = (raw >> 4) / 15.0f, blkv = (raw & 15) / 15.0f;
                    float p[4][3], sh[4], sk[4], bl[4];
                    for (int v = 0; v < 4; v++) {
                        const int* c = FACE_VERT[f][v];
                        float vy = (float)c[1];
                        if (topOpen && c[1] == 1) vy = 0.875f;
                        p[v][0] = (float)(x + c[0]);
                        p[v][1] = (float)y + vy;
                        p[v][2] = (float)(z + c[2]);
                        sh[v] = FACE_SHADE[f];
                        sk[v] = skyv;
                        bl[v] = blkv;
                    }
                    water.quad(p, FACE_UV[f], def.tex[f], sh, sk, bl, false);
                }
            }

    // ---- pass 2: greedy-meshed cubes (opaque + cutout)
    const int sizes[3] = {CX, CY, CZ};
    static thread_local std::vector<FaceCell> mask;
    for (int dir = 0; dir < 6; dir++) {
        int nAx = dir / 2;
        int ua = UAXIS[nAx], va = VAXIS[nAx];
        int su = sizes[ua], sv = sizes[va];
        mask.resize((size_t)su * sv);

        for (int slice = 0; slice < sizes[nAx]; slice++) {
            bool any = false;
            for (int v = 0; v < sv; v++)
                for (int u = 0; u < su; u++) {
                    FaceCell& fc = mask[(size_t)v * su + u];
                    fc.present = 0;
                    int p[3];
                    p[nAx] = slice; p[ua] = u; p[va] = v;
                    uint8_t b = chunk.get(p[0], p[1], p[2]);
                    if (b == B_AIR) continue;
                    const BlockDef& def = g_blocks[b];
                    if (def.cross || (def.fluid && dir != 2)) continue;
                    int q[3] = {n.bx + p[0] + FACE_NORMAL[dir][0],
                                p[1] + FACE_NORMAL[dir][1],
                                n.bz + p[2] + FACE_NORMAL[dir][2]};
                    if (!faceVisible(b, n.block(q[0], q[1], q[2]))) continue;
                    fc.present = 1;
                    fc.fluid = def.fluid ? 1 : 0;
                    fc.layer = def.tex[dir];
                    if (def.fluid) { // flat light from the cell above, no AO
                        uint8_t raw = n.rawLight(q[0], q[1], q[2]);
                        for (int ci = 0; ci < 4; ci++) {
                            fc.ao[ci] = 3;
                            fc.sky4[ci] = (uint8_t)(4 * (raw >> 4));
                            fc.blk4[ci] = (uint8_t)(4 * (raw & 15));
                        }
                    } else {
                        sampleFace(n, dir, q, fc);
                    }
                    any = true;
                }
            if (!any) continue;

            for (int v = 0; v < sv; v++)
                for (int u = 0; u < su; u++) {
                    FaceCell& fc = mask[(size_t)v * su + u];
                    if (!fc.present) continue;
                    int w = 1, h = 1;
                    if (fc.uniform()) {
                        while (u + w < su && mergeable(fc, mask[(size_t)v * su + u + w])) w++;
                        bool grow = true;
                        while (grow && v + h < sv) {
                            for (int k = 0; k < w; k++)
                                if (!mergeable(fc, mask[(size_t)(v + h) * su + u + k])) { grow = false; break; }
                            if (grow) h++;
                        }
                    }
                    emitRect(fc.fluid ? water : opaque, dir, slice, u, v, w, h, fc);
                    for (int dv2 = 0; dv2 < h; dv2++)
                        for (int du2 = 0; du2 < w; du2++)
                            mask[(size_t)(v + dv2) * su + u + du2].present = 0;
                }
        }
    }

    uploadPart(chunk.vao, chunk.vbo, chunk.ebo, chunk.indexCount, opaque);
    uploadPart(chunk.wvao, chunk.wvbo, chunk.webo, chunk.windexCount, water);
    chunk.dirty = false;
}
