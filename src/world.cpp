#include "world.h"
#include "noise.h"
#include <algorithm>
#include <cmath>

void Chunk::computeHeightColumn(int x, int z) {
    int y = CY - 1;
    while (y >= 0 && !blockOpaque(get(x, y, z))) y--;
    height[z * CX + x] = (int16_t)(y + 1);
}

void Chunk::computeHeight() {
    for (int z = 0; z < CZ; z++)
        for (int x = 0; x < CX; x++)
            computeHeightColumn(x, z);
}

void Chunk::deleteMesh() {
    if (vao) { glDeleteVertexArrays(1, &vao); glDeleteBuffers(1, &vbo); glDeleteBuffers(1, &ebo); }
    if (wvao) { glDeleteVertexArrays(1, &wvao); glDeleteBuffers(1, &wvbo); glDeleteBuffers(1, &webo); }
    vao = vbo = ebo = wvao = wvbo = webo = 0;
    indexCount = windexCount = 0;
}

Chunk* World::chunk(int cx, int cz) {
    auto it = chunks.find(chunkKey(cx, cz));
    return it == chunks.end() ? nullptr : it->second.get();
}

const Chunk* World::chunk(int cx, int cz) const {
    auto it = chunks.find(chunkKey(cx, cz));
    return it == chunks.end() ? nullptr : it->second.get();
}

Chunk* World::ensure(int cx, int cz) {
    Chunk* c = chunk(cx, cz);
    if (!c) {
        auto up = std::make_unique<Chunk>();
        c = up.get();
        c->cx = cx;
        c->cz = cz;
        chunks[chunkKey(cx, cz)] = std::move(up);
    }
    if (!c->generated) generate(*c);
    return c;
}

uint8_t World::block(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= CY) return B_AIR;
    const Chunk* c = chunk(wx >> 4, wz >> 4);
    if (!c || !c->generated) return B_AIR;
    return c->get(wx & 15, wy, wz & 15);
}

// ------------------------------------------------------------------ lighting

int World::skyLight(int wx, int wy, int wz) const {
    if (wy >= CY) return 15;
    if (wy < 0) return 0;
    const Chunk* c = chunk(wx >> 4, wz >> 4);
    if (!c || !c->generated) return 0;
    return c->light[blockIndex(wx & 15, wy, wz & 15)] >> 4;
}

int World::blockLight(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= CY) return 0;
    const Chunk* c = chunk(wx >> 4, wz >> 4);
    if (!c || !c->generated) return 0;
    return c->light[blockIndex(wx & 15, wy, wz & 15)] & 15;
}

void World::markLightDirty(Chunk* c, int lx, int lz) {
    c->dirty = true;
    // Light within 1 cell of a border affects the neighbor's smooth lighting.
    int dxs[2] = {lx == 0 ? -1 : 0, lx == 15 ? 1 : 0};
    int dzs[2] = {lz == 0 ? -1 : 0, lz == 15 ? 1 : 0};
    for (int dx : dxs)
        for (int dz : dzs) {
            if (!dx && !dz) continue;
            if (Chunk* n = chunk(c->cx + dx, c->cz + dz)) n->dirty = true;
        }
}

void World::setSky(int wx, int wy, int wz, int l) {
    if (wy < 0 || wy >= CY) return;
    Chunk* c = chunk(wx >> 4, wz >> 4);
    if (!c || !c->generated) return;
    int lx = wx & 15, lz = wz & 15;
    uint8_t& v = c->light[blockIndex(lx, wy, lz)];
    v = (uint8_t)((v & 0x0F) | (l << 4));
    markLightDirty(c, lx, lz);
}

void World::setBlk(int wx, int wy, int wz, int l) {
    if (wy < 0 || wy >= CY) return;
    Chunk* c = chunk(wx >> 4, wz >> 4);
    if (!c || !c->generated) return;
    int lx = wx & 15, lz = wz & 15;
    uint8_t& v = c->light[blockIndex(lx, wy, lz)];
    v = (uint8_t)((v & 0xF0) | l);
    markLightDirty(c, lx, lz);
}

static const int DIR6[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};

void World::processLight() {
    // Removal passes first: they seed the addition queues at their frontier.
    while (!skyRem_.empty()) {
        LRem r = skyRem_.front(); skyRem_.pop();
        for (int d = 0; d < 6; d++) {
            int nx = r.x + DIR6[d][0], ny = r.y + DIR6[d][1], nz = r.z + DIR6[d][2];
            int nl = skyLight(nx, ny, nz);
            bool down = DIR6[d][1] == -1;
            if (nl != 0 && (nl < r.level || (down && r.level == 15 && nl == 15))) {
                setSky(nx, ny, nz, 0);
                skyRem_.push({nx, ny, nz, nl});
            } else if (nl >= r.level && nl > 0) {
                skyAdd_.push({nx, ny, nz});
            }
        }
    }
    while (!blkRem_.empty()) {
        LRem r = blkRem_.front(); blkRem_.pop();
        for (int d = 0; d < 6; d++) {
            int nx = r.x + DIR6[d][0], ny = r.y + DIR6[d][1], nz = r.z + DIR6[d][2];
            int nl = blockLight(nx, ny, nz);
            if (nl != 0 && nl < r.level) {
                setBlk(nx, ny, nz, 0);
                blkRem_.push({nx, ny, nz, nl});
            } else if (nl >= r.level && nl > 0) {
                blkAdd_.push({nx, ny, nz});
            }
        }
    }
    while (!skyAdd_.empty()) {
        LNode p = skyAdd_.front(); skyAdd_.pop();
        int l = skyLight(p.x, p.y, p.z);
        if (l <= 1) continue;
        for (int d = 0; d < 6; d++) {
            int nx = p.x + DIR6[d][0], ny = p.y + DIR6[d][1], nz = p.z + DIR6[d][2];
            int o = lightOpacity(block(nx, ny, nz));
            if (o >= 15) continue;
            bool down = DIR6[d][1] == -1;
            int target = (down && l == 15 && o == 1) ? 15 : l - o;
            if (target > skyLight(nx, ny, nz)) {
                setSky(nx, ny, nz, target);
                skyAdd_.push({nx, ny, nz});
            }
        }
    }
    while (!blkAdd_.empty()) {
        LNode p = blkAdd_.front(); blkAdd_.pop();
        int l = blockLight(p.x, p.y, p.z);
        if (l <= 1) continue;
        for (int d = 0; d < 6; d++) {
            int nx = p.x + DIR6[d][0], ny = p.y + DIR6[d][1], nz = p.z + DIR6[d][2];
            int o = lightOpacity(block(nx, ny, nz));
            if (o >= 15) continue;
            if (l - o > blockLight(nx, ny, nz)) {
                setBlk(nx, ny, nz, l - o);
                blkAdd_.push({nx, ny, nz});
            }
        }
    }
}

void World::skyColumnAdd(int wx, int wz) {
    Chunk* c = chunk(wx >> 4, wz >> 4);
    if (!c || !c->generated) return;
    int lx = wx & 15, lz = wz & 15;
    int level = 15;
    for (int y = CY - 1; y >= 0 && level > 0; y--) {
        int o = lightOpacity(c->get(lx, y, lz));
        if (o >= 15) break;
        if (!(level == 15 && o == 1)) level = std::max(0, level - o);
        int idx = blockIndex(lx, y, lz);
        if ((c->light[idx] >> 4) < level) {
            c->light[idx] = (uint8_t)((c->light[idx] & 0x0F) | (level << 4));
            markLightDirty(c, lx, lz);
            skyAdd_.push({wx, y, wz});
        }
    }
}

void World::initChunkLight(Chunk& c) {
    int bx = c.cx * CX, bz = c.cz * CZ;
    int maxH = 0;
    for (int i = 0; i < CX * CZ; i++) maxH = std::max(maxH, (int)c.height[i]);

    for (int z = 0; z < CZ; z++)
        for (int x = 0; x < CX; x++) {
            int level = 15;
            for (int y = CY - 1; y >= 0; y--) {
                int o = lightOpacity(c.get(x, y, z));
                if (o >= 15) level = 0;
                else if (!(level == 15 && o == 1)) level = std::max(0, level - o);
                c.light[blockIndex(x, y, z)] = (uint8_t)(level << 4);
            }
        }

    // Seed sideways spreading: any lit cell next to a darker in-chunk cell.
    int yTop = std::min(CY - 1, maxH + 1);
    for (int z = 0; z < CZ; z++)
        for (int x = 0; x < CX; x++)
            for (int y = yTop; y >= 0; y--) {
                int l = c.light[blockIndex(x, y, z)] >> 4;
                if (l <= 1) continue;
                for (int d = 0; d < 6; d++) {
                    int nx = x + DIR6[d][0], ny = y + DIR6[d][1], nz = z + DIR6[d][2];
                    if (nx < 0 || nx >= CX || ny < 0 || ny >= CY || nz < 0 || nz >= CZ) continue;
                    if (lightOpacity(c.get(nx, ny, nz)) >= 15) continue;
                    if ((c.light[blockIndex(nx, ny, nz)] >> 4) < l - 1) {
                        skyAdd_.push({bx + x, y, bz + z});
                        break;
                    }
                }
            }
}

void World::pullLightBorders(Chunk& c) {
    int bx = c.cx * CX, bz = c.cz * CZ;
    struct Side { int dx, dz; };
    const Side sides[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (const Side& s : sides) {
        Chunk* n = chunk(c.cx + s.dx, c.cz + s.dz);
        if (!n || !n->generated) continue;
        for (int t = 0; t < 16; t++)
            for (int y = 0; y < CY; y++) {
                int ax, az; // border cell in c (world coords)
                if (s.dx) { ax = bx + (s.dx > 0 ? 15 : 0); az = bz + t; }
                else      { ax = bx + t; az = bz + (s.dz > 0 ? 15 : 0); }
                int nx = ax + s.dx, nz = az + s.dz; // adjacent cell in n
                uint8_t la = c.light[blockIndex(ax & 15, y, az & 15)];
                uint8_t lb = n->light[blockIndex(nx & 15, y, nz & 15)];
                int sa = la >> 4, sb = lb >> 4;
                if (sa > sb + 1) skyAdd_.push({ax, y, az});
                else if (sb > sa + 1) skyAdd_.push({nx, y, nz});
                int ba = la & 15, bb = lb & 15;
                if (ba > bb + 1) blkAdd_.push({ax, y, az});
                else if (bb > ba + 1) blkAdd_.push({nx, y, nz});
            }
    }
}

void World::setBlock(int wx, int wy, int wz, uint8_t b) {
    if (wy < 0 || wy >= CY) return;
    Chunk* c = chunk(wx >> 4, wz >> 4);
    if (!c || !c->generated) return;
    int lx = wx & 15, lz = wz & 15;
    c->set(lx, wy, lz, b);
    c->computeHeightColumn(lx, lz);
    c->dirty = true;
    // Border edits affect neighbor meshes (face culling + AO uses diagonals).
    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) {
            if (!dx && !dz) continue;
            bool xEdge = (dx == -1 && lx <= 1) || (dx == 1 && lx >= 14) || dx == 0;
            bool zEdge = (dz == -1 && lz <= 1) || (dz == 1 && lz >= 14) || dz == 0;
            if (xEdge && zEdge && (dx || dz)) {
                if (Chunk* n = chunk(c->cx + dx, c->cz + dz)) n->dirty = true;
            }
        }

    // Incremental light update: remove old contributions, re-add sources.
    int idx = blockIndex(lx, wy, lz);
    int oldSky = c->light[idx] >> 4;
    int oldBlk = c->light[idx] & 15;
    if (oldSky > 0) {
        c->light[idx] &= 0x0F;
        skyRem_.push({wx, wy, wz, oldSky});
    }
    if (oldBlk > 0) {
        c->light[idx] &= 0xF0;
        blkRem_.push({wx, wy, wz, oldBlk});
    }
    if (g_blocks[b].emission > 0) {
        c->light[idx] = (uint8_t)((c->light[idx] & 0xF0) | g_blocks[b].emission);
        blkAdd_.push({wx, wy, wz});
    }
    skyColumnAdd(wx, wz);
    for (int d = 0; d < 6; d++) {
        skyAdd_.push({wx + DIR6[d][0], wy + DIR6[d][1], wz + DIR6[d][2]});
        blkAdd_.push({wx + DIR6[d][0], wy + DIR6[d][1], wz + DIR6[d][2]});
    }
    processLight();
}

// ------------------------------------------------------------------ terrain

int World::topSolidY(int wx, int wz) const {
    const Chunk* c = chunk(wx >> 4, wz >> 4);
    if (!c || !c->generated) return -1;
    return c->height[(wz & 15) * CX + (wx & 15)] - 1;
}

int World::terrainHeight(int wx, int wz) const {
    float x = (float)wx, z = (float)wz;
    float base = fbm2(seed + 11, x * 0.003f, z * 0.003f, 4);   // continents
    float hills = fbm2(seed + 17, x * 0.009f, z * 0.009f, 4);  // rolling hills
    float detail = fbm2(seed + 23, x * 0.02f, z * 0.02f, 3);
    float m = fbm2(seed + 37, x * 0.0045f, z * 0.0045f, 5);    // mountain mask
    float mt = std::clamp((m - 0.52f) / 0.26f, 0.0f, 1.0f);
    mt = mt * mt * (3.0f - 2.0f * mt);
    float h = 30.0f + base * 36.0f + (hills - 0.5f) * 18.0f + (detail - 0.5f) * 8.0f + mt * mt * 80.0f;
    return std::clamp((int)h, 4, CY - 12);
}

static bool caveCarved(uint32_t seed, int x, int y, int z) {
    float n1 = fbm3(seed + 51, x * 0.035f, y * 0.05f, z * 0.035f, 2);
    float n2 = fbm3(seed + 67, x * 0.035f, y * 0.05f, z * 0.035f, 2);
    if (std::fabs(n1 - 0.5f) < 0.052f && std::fabs(n2 - 0.5f) < 0.052f) return true;
    float n3 = fbm3(seed + 83, x * 0.02f, y * 0.04f, z * 0.02f, 2);
    return n3 > 0.78f;
}

bool World::treeAt(int wx, int wz, int& trunkH) const {
    uint32_t r = hash2i(wx, wz, seed + 777);
    float forest = fbm2(seed + 91, wx * 0.006f, wz * 0.006f, 3);
    uint32_t density = forest > 0.55f ? 28 : 170;
    if (r % density != 0) return false;
    trunkH = 4 + (int)((r / 1024u) % 3); // 4..6
    return true;
}

void World::placeTreesInto(Chunk& c) {
    int bx = c.cx * CX, bz = c.cz * CZ;
    for (int tz = bz - 3; tz < bz + CZ + 3; tz++)
        for (int tx = bx - 3; tx < bx + CX + 3; tx++) {
            int trunkH;
            if (!treeAt(tx, tz, trunkH)) continue;
            int h = terrainHeight(tx, tz);
            if (h < SEA + 2 || h > 78) continue;
            if (caveCarved(seed, tx, h, tz)) continue;
            int drop = std::max(std::max(h - terrainHeight(tx + 1, tz), h - terrainHeight(tx - 1, tz)),
                                std::max(h - terrainHeight(tx, tz + 1), h - terrainHeight(tx, tz - 1)));
            if (h > 62 && drop >= 4) continue; // bare stone slope
            int top = h + trunkH;
            if (top + 2 >= CY) continue;

            auto put = [&](int wx, int wy, int wz, uint8_t b, bool onlyAir) {
                int lx = wx - bx, lz = wz - bz;
                if (lx < 0 || lx >= CX || lz < 0 || lz >= CZ || wy < 0 || wy >= CY) return;
                if (onlyAir && c.get(lx, wy, lz) != B_AIR) return;
                c.set(lx, wy, lz, b);
            };

            // Canopy (leaves only into air)
            for (int ly = top - 1; ly <= top + 2; ly++) {
                int r = (ly <= top) ? 2 : 1;
                for (int dz = -r; dz <= r; dz++)
                    for (int dx = -r; dx <= r; dx++) {
                        if (dx == 0 && dz == 0 && ly <= top) continue; // trunk goes here
                        bool corner = std::abs(dx) == r && std::abs(dz) == r;
                        if (ly == top + 2 && std::abs(dx) + std::abs(dz) > 1) continue;
                        if (corner && ly <= top && (hash3i(tx + dx, ly, tz + dz, seed + 5) & 1)) continue;
                        if (corner && ly == top + 1) continue;
                        put(tx + dx, ly, tz + dz, B_OAK_LEAVES, true);
                    }
            }
            // Trunk
            put(tx, h, tz, B_DIRT, false);
            for (int ly = h + 1; ly <= top; ly++) put(tx, ly, tz, B_OAK_LOG, false);
        }
}

void World::generate(Chunk& c) {
    int bx = c.cx * CX, bz = c.cz * CZ;
    for (int z = 0; z < CZ; z++)
        for (int x = 0; x < CX; x++) {
            int wx = bx + x, wz = bz + z;
            int h = terrainHeight(wx, wz);
            bool beach = h <= SEA + 1;
            bool snowy = h >= 82;
            bool peak = h >= 95;
            // Steep high slopes expose bare stone instead of grass/dirt.
            int drop = std::max(std::max(h - terrainHeight(wx + 1, wz), h - terrainHeight(wx - 1, wz)),
                                std::max(h - terrainHeight(wx, wz + 1), h - terrainHeight(wx, wz - 1)));
            bool rocky = h > 62 && drop >= 4;

            for (int y = 0; y <= h; y++) {
                uint8_t b;
                if (y == 0) b = B_BEDROCK;
                else if (y <= 2 && (hash3i(wx, y, wz, seed + 1) & 3) == 0) b = B_BEDROCK;
                else if (y >= h - 3 && beach) b = B_SAND;
                else if (y >= h - 3 && rocky) b = B_STONE;
                else if (y == h) b = peak ? B_SNOW : (snowy ? B_SNOWY_GRASS : B_GRASS);
                else if (y >= h - 3) b = B_DIRT;
                else {
                    b = B_STONE;
                    uint32_t r = hash3i(wx, y, wz, seed + 2);
                    if (y < 70 && r % 90 == 0) b = B_COAL_ORE;
                    else if (y < 54 && r % 110 == 1) b = B_IRON_ORE;
                    else if (y < 16 && r % 400 == 2) b = B_DIAMOND_ORE;
                }
                // Caves (keep a solid floor near bedrock and a roof under the surface)
                if (y > 3 && y < h - 4 && b != B_BEDROCK && caveCarved(seed, wx, y, wz)) b = B_AIR;
                c.set(x, y, z, b);
            }
            // Water
            for (int y = h + 1; y <= SEA; y++) c.set(x, y, z, B_WATER);

            // Decorations
            if (!beach && !snowy && !rocky && h > SEA + 1 && h + 1 < CY && c.get(x, h, z) == B_GRASS) {
                uint32_t r = hash2i(wx, wz, seed + 3);
                if (r % 9 == 0) c.set(x, h + 1, z, B_SHORT_GRASS);
                else if (r % 141 == 1) c.set(x, h + 1, z, B_POPPY);
                else if (r % 141 == 2) c.set(x, h + 1, z, B_DANDELION);
            }
        }

    placeTreesInto(c);
    c.computeHeight();
    c.generated = true;
    c.dirty = true;
    initChunkLight(c);
    pullLightBorders(c);
}

bool World::neighborsGenerated(int cx, int cz) const {
    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) {
            if (!dx && !dz) continue;
            const Chunk* n = chunk(cx + dx, cz + dz);
            if (!n || !n->generated) return false;
        }
    return true;
}

void World::unloadFar(int pcx, int pcz, int radius) {
    for (auto it = chunks.begin(); it != chunks.end();) {
        Chunk* c = it->second.get();
        if (std::max(std::abs(c->cx - pcx), std::abs(c->cz - pcz)) > radius) {
            c->deleteMesh();
            it = chunks.erase(it);
        } else {
            ++it;
        }
    }
}

bool World::raycast(glm::vec3 o, glm::vec3 d, float maxDist,
                    glm::ivec3& hitBlock, glm::ivec3& prevBlock) const {
    // Amanatides & Woo voxel traversal.
    glm::ivec3 cell(std::floor(o.x), std::floor(o.y), std::floor(o.z));
    glm::ivec3 step(d.x > 0 ? 1 : -1, d.y > 0 ? 1 : -1, d.z > 0 ? 1 : -1);
    glm::vec3 tDelta(std::abs(d.x) > 1e-8f ? 1.0f / std::abs(d.x) : 1e30f,
                     std::abs(d.y) > 1e-8f ? 1.0f / std::abs(d.y) : 1e30f,
                     std::abs(d.z) > 1e-8f ? 1.0f / std::abs(d.z) : 1e30f);
    auto frac = [](float v) { return v - std::floor(v); };
    glm::vec3 tMax(
        tDelta.x * (d.x > 0 ? 1.0f - frac(o.x) : frac(o.x)),
        tDelta.y * (d.y > 0 ? 1.0f - frac(o.y) : frac(o.y)),
        tDelta.z * (d.z > 0 ? 1.0f - frac(o.z) : frac(o.z)));

    glm::ivec3 prev = cell;
    float t = 0;
    while (t <= maxDist) {
        uint8_t b = block(cell.x, cell.y, cell.z);
        if (b != B_AIR && !g_blocks[b].fluid) {
            hitBlock = cell;
            prevBlock = prev;
            return true;
        }
        prev = cell;
        if (tMax.x < tMax.y && tMax.x < tMax.z) { cell.x += step.x; t = tMax.x; tMax.x += tDelta.x; }
        else if (tMax.y < tMax.z)               { cell.y += step.y; t = tMax.y; tMax.y += tDelta.y; }
        else                                    { cell.z += step.z; t = tMax.z; tMax.z += tDelta.z; }
    }
    return false;
}
