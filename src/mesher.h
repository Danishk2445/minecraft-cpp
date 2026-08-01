#pragma once
#include "world.h"

// Builds (or rebuilds) the GPU meshes for a chunk. All 8 neighbor chunks must
// be generated. Vertex layout: x,y,z, u,v, layer, light (7 floats).
void buildChunkMesh(World& world, Chunk& chunk);
