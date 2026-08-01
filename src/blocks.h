#pragma once
#include <cstdint>

class BlockAtlas;

enum : uint8_t {
    B_AIR = 0,
    B_GRASS,
    B_DIRT,
    B_STONE,
    B_COBBLESTONE,
    B_SAND,
    B_GRAVEL,
    B_BEDROCK,
    B_OAK_LOG,
    B_OAK_PLANKS,
    B_OAK_LEAVES,
    B_GLASS,
    B_WATER,
    B_BRICKS,
    B_STONE_BRICKS,
    B_SANDSTONE,
    B_SNOWY_GRASS,
    B_SNOW,
    B_COAL_ORE,
    B_IRON_ORE,
    B_DIAMOND_ORE,
    B_SHORT_GRASS,
    B_POPPY,
    B_DANDELION,
    B_TORCH,
    B_COUNT
};

// Face order: 0 +X, 1 -X, 2 +Y (top), 3 -Y (bottom), 4 +Z, 5 -Z
struct BlockDef {
    const char* name;
    uint16_t tex[6];
    bool opaque; // full opaque cube (culls neighbor faces, casts AO, blocks sky)
    bool solid;  // has collision
    bool cutout; // alpha-tested (leaves, glass)
    bool cross;  // X-shaped plant
    bool fluid;  // water
    uint8_t emission; // block light emitted (0-15)
};

extern BlockDef g_blocks[B_COUNT];

// Loads all needed textures into the atlas and fills g_blocks[].tex.
void initBlocks(BlockAtlas& atlas);

inline bool blockOpaque(uint8_t b) { return g_blocks[b].opaque; }
inline bool blockSolid(uint8_t b) { return g_blocks[b].solid; }
