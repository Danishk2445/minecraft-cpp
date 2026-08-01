#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <queue>
#include <unordered_map>
#include "blocks.h"

constexpr int CX = 16;   // chunk width  (x)
constexpr int CY = 128;  // chunk height (y)
constexpr int CZ = 16;   // chunk depth  (z)
constexpr int SEA = 44;  // sea level: water fills up to and including this y

inline int blockIndex(int x, int y, int z) { return (y * CZ + z) * CX + x; }
inline int64_t chunkKey(int cx, int cz) { return ((int64_t)cx << 32) ^ (uint32_t)cz; }

// How much a block dims light passing through it (1 = transparent step cost).
inline int lightOpacity(uint8_t b) {
    if (g_blocks[b].opaque) return 15;
    if (b == B_WATER) return 3;
    if (b == B_OAK_LEAVES) return 2;
    return 1;
}

struct Chunk {
    int cx = 0, cz = 0;
    uint8_t blocks[CX * CY * CZ] = {};
    uint8_t light[CX * CY * CZ] = {}; // high nibble: sky light, low nibble: block light
    int16_t height[CX * CZ] = {};     // y of highest opaque block + 1
    bool generated = false;
    bool dirty = true; // needs (re)meshing

    // GPU mesh
    GLuint vao = 0, vbo = 0, ebo = 0;
    GLsizei indexCount = 0;
    GLuint wvao = 0, wvbo = 0, webo = 0;
    GLsizei windexCount = 0;

    uint8_t get(int x, int y, int z) const { return blocks[blockIndex(x, y, z)]; }
    void set(int x, int y, int z, uint8_t b) { blocks[blockIndex(x, y, z)] = b; }
    void computeHeightColumn(int x, int z);
    void computeHeight();
    void deleteMesh();
};

class World {
public:
    uint32_t seed = 1337;

    Chunk* chunk(int cx, int cz);
    const Chunk* chunk(int cx, int cz) const;
    Chunk* ensure(int cx, int cz); // create + generate if needed

    uint8_t block(int wx, int wy, int wz) const; // B_AIR outside loaded chunks
    void setBlock(int wx, int wy, int wz, uint8_t b);

    int skyLight(int wx, int wy, int wz) const;   // 0-15 (15 above the world)
    int blockLight(int wx, int wy, int wz) const; // 0-15
    void processLight(); // drain the propagation queues

    int terrainHeight(int wx, int wz) const;
    int topSolidY(int wx, int wz) const; // y of highest opaque block, -1 if unloaded
    bool raycast(glm::vec3 origin, glm::vec3 dir, float maxDist,
                 glm::ivec3& hitBlock, glm::ivec3& prevBlock) const;

    void generate(Chunk& c);
    bool neighborsGenerated(int cx, int cz) const;
    void unloadFar(int pcx, int pcz, int radius);

    std::unordered_map<int64_t, std::unique_ptr<Chunk>> chunks;

private:
    struct LNode { int x, y, z; };
    struct LRem { int x, y, z; int level; };
    std::queue<LNode> skyAdd_, blkAdd_;
    std::queue<LRem> skyRem_, blkRem_;

    void setSky(int wx, int wy, int wz, int l);
    void setBlk(int wx, int wy, int wz, int l);
    void markLightDirty(Chunk* c, int lx, int lz);
    void initChunkLight(Chunk& c);
    void pullLightBorders(Chunk& c);
    void skyColumnAdd(int wx, int wz); // re-fill a column from the sky (additive)

    bool treeAt(int wx, int wz, int& trunkH) const;
    void placeTreesInto(Chunk& c);
};
