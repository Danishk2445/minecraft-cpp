// Placement.h -- vanilla getStateForPlacement: the state(s) a block item places
// given where the player clicked and how it is looking, plus hand breaking speed.
#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "core/Math.h"
#include "world/block/BlockGetter.h"

namespace mc {

class Player;

struct PlaceContext {
    BlockPos pos;               // where the block goes
    Direction clickedFace;      // face of the clicked block (pointing toward `pos` unless replacing)
    glm::dvec3 hitPoint{0.0};   // world position of the click
    float yaw = 0.0f, pitch = 0.0f;
    bool replacingClicked = false; // the clicked block itself was replaceable (grass, snow layer)
    bool sneaking = false;
};

// Vanilla Direction.fromYRot: the horizontal direction the player faces.
Direction horizontalFacing(float yaw);
// Vanilla getNearestLookingDirection: the axis direction closest to the look vector.
Direction nearestLookingDirection(float yaw, float pitch);

// The blocks to set (two for doors, beds and double plants), or empty if the item
// cannot be placed there (no support, no room). `block` is the item's block.
std::vector<std::pair<BlockPos, StateId>> placementStates(const BlockRegistry& reg, const BlockGetter& level,
                                                          const Block& block, const PlaceContext& ctx);

// If clicking `clicked` with `block` merges into it (bottom slab + slab on its top
// face, ...), the merged state; vanilla canBeReplaced for slabs.
std::optional<StateId> mergeIntoClicked(const BlockRegistry& reg, StateId clicked, const Block& block,
                                        Direction clickedFace, double hitYInBlock);


} // namespace mc
