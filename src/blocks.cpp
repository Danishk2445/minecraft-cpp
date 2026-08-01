#include "blocks.h"
#include "texture.h"

BlockDef g_blocks[B_COUNT];

// Baked biome tints (plains biome values from vanilla colormaps).
static const int GRASS_R = 0x91, GRASS_G = 0xBD, GRASS_B = 0x59;
static const int FOLIAGE_R = 0x77, FOLIAGE_G = 0xAB, FOLIAGE_B = 0x2F;
static const int WATER_R = 0x3F, WATER_G = 0x76, WATER_B = 0xE4;

static void def(uint8_t id, const char* name,
                uint16_t e, uint16_t w, uint16_t t, uint16_t b, uint16_t s, uint16_t n,
                bool opaque, bool solid, bool cutout = false, bool cross = false, bool fluid = false) {
    g_blocks[id] = {name, {e, w, t, b, s, n}, opaque, solid, cutout, cross, fluid};
}

static void defUniform(uint8_t id, const char* name, uint16_t tex,
                       bool opaque, bool solid, bool cutout = false, bool cross = false, bool fluid = false) {
    def(id, name, tex, tex, tex, tex, tex, tex, opaque, solid, cutout, cross, fluid);
}

void initBlocks(BlockAtlas& a) {
    uint16_t dirt = a.addFile("dirt");
    uint16_t grassTop = a.add(tintImage(a.load("grass_block_top"), GRASS_R, GRASS_G, GRASS_B));

    Image grassSide = a.load("grass_block_side");
    compositeOver(grassSide, tintImage(a.load("grass_block_side_overlay"), GRASS_R, GRASS_G, GRASS_B));
    uint16_t grassSideL = a.add(grassSide);

    uint16_t stone = a.addFile("stone");
    uint16_t cobble = a.addFile("cobblestone");
    uint16_t sand = a.addFile("sand");
    uint16_t gravel = a.addFile("gravel");
    uint16_t bedrock = a.addFile("bedrock");
    uint16_t logSide = a.addFile("oak_log");
    uint16_t logTop = a.addFile("oak_log_top");
    uint16_t planks = a.addFile("oak_planks");
    uint16_t leaves = a.add(tintImage(a.load("oak_leaves"), FOLIAGE_R, FOLIAGE_G, FOLIAGE_B));
    uint16_t glass = a.addFile("glass");
    uint16_t bricks = a.addFile("bricks");
    uint16_t stoneBricks = a.addFile("stone_bricks");
    uint16_t sandstone = a.addFile("sandstone");
    uint16_t sandstoneTop = a.addFile("sandstone_top");
    uint16_t sandstoneBot = a.addFile("sandstone_bottom");
    uint16_t snow = a.addFile("snow");
    uint16_t grassSnow = a.addFile("grass_block_snow");
    uint16_t coalOre = a.addFile("coal_ore");
    uint16_t ironOre = a.addFile("iron_ore");
    uint16_t diamondOre = a.addFile("diamond_ore");
    uint16_t shortGrass = a.add(tintImage(a.load("short_grass"), GRASS_R, GRASS_G, GRASS_B));
    uint16_t poppy = a.addFile("poppy");
    uint16_t dandelion = a.addFile("dandelion");
    uint16_t torch = a.addFile("torch");

    // Water: 32-frame animated strip, tinted; frametime 2 ticks = 0.1s.
    uint16_t water = a.addAnimated(tintImage(a.load("water_still"), WATER_R, WATER_G, WATER_B), 0.1f);

    defUniform(B_AIR, "air", 0, false, false);
    def(B_GRASS, "grass block", grassSideL, grassSideL, grassTop, dirt, grassSideL, grassSideL, true, true);
    defUniform(B_DIRT, "dirt", dirt, true, true);
    defUniform(B_STONE, "stone", stone, true, true);
    defUniform(B_COBBLESTONE, "cobblestone", cobble, true, true);
    defUniform(B_SAND, "sand", sand, true, true);
    defUniform(B_GRAVEL, "gravel", gravel, true, true);
    defUniform(B_BEDROCK, "bedrock", bedrock, true, true);
    def(B_OAK_LOG, "oak log", logSide, logSide, logTop, logTop, logSide, logSide, true, true);
    defUniform(B_OAK_PLANKS, "oak planks", planks, true, true);
    defUniform(B_OAK_LEAVES, "oak leaves", leaves, false, true, true);
    defUniform(B_GLASS, "glass", glass, false, true, true);
    defUniform(B_WATER, "water", water, false, false, false, false, true);
    defUniform(B_BRICKS, "bricks", bricks, true, true);
    defUniform(B_STONE_BRICKS, "stone bricks", stoneBricks, true, true);
    def(B_SANDSTONE, "sandstone", sandstone, sandstone, sandstoneTop, sandstoneBot, sandstone, sandstone, true, true);
    def(B_SNOWY_GRASS, "snowy grass", grassSnow, grassSnow, snow, dirt, grassSnow, grassSnow, true, true);
    defUniform(B_SNOW, "snow", snow, true, true);
    defUniform(B_COAL_ORE, "coal ore", coalOre, true, true);
    defUniform(B_IRON_ORE, "iron ore", ironOre, true, true);
    defUniform(B_DIAMOND_ORE, "diamond ore", diamondOre, true, true);
    defUniform(B_SHORT_GRASS, "short grass", shortGrass, false, false, true, true);
    defUniform(B_POPPY, "poppy", poppy, false, false, true, true);
    defUniform(B_DANDELION, "dandelion", dandelion, false, false, true, true);
    defUniform(B_TORCH, "torch", torch, false, false, true, true);
    g_blocks[B_TORCH].emission = 14;
}
