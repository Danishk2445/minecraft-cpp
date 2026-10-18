// PathTypes.h -- what each block is to a walking mob (vanilla PathType and
// WalkNodeEvaluator.getPathTypeFromState / getPathTypeStatic), and the block
// properties path finding and spawning ask about (isPathfindable, isSolid).
#pragma once

#include <array>
#include <cstdint>

#include "core/Math.h"
#include "world/block/BlockRegistry.h"

namespace mc {

class BlockGetter;

// Vanilla PathType, with its default malus (getMalus).
enum class PathType : uint8_t {
    Blocked,
    Open,
    Walkable,
    WalkableDoor,
    Trapdoor,
    PowderSnow,
    DangerPowderSnow,
    Fence,
    Lava,
    Water,
    WaterBorder,
    Rail,
    UnpassableRail,
    DangerFire,
    DamageFire,
    DangerOther,
    DamageOther,
    DoorOpen,
    DoorWoodClosed,
    DoorIronClosed,
    Breach,
    Leaves,
    StickyHoney,
    Cocoa,
    DamageCautious,
    DangerTrapdoor,
    Count,
};
constexpr size_t kPathTypeCount = size_t(PathType::Count);
float defaultMalus(PathType type);
using PathMalus = std::array<float, kPathTypeCount>;
PathMalus defaultMaluses();

// Per-state facts, cached per registry.
struct PathBlockInfo {
    PathType type = PathType::Open;  // vanilla getPathTypeFromState
    bool pathfindable = true;        // isPathfindable(LAND): a mob can stand in its space
    bool solid = false;              // vanilla isSolid (legacy "solid" material)
    bool burning = false;            // NodeEvaluator.isBurningBlock: fire, lava, magma, lit campfires
};
const PathBlockInfo& pathBlockInfo(const BlockRegistry& registry, StateId state);

// Vanilla getPathTypeStatic: the type of the space at p, looking below (a floor makes open
// air WALKABLE) and around it (fire, water and the like nearby make it DANGER/BORDER).
PathType pathTypeAt(const BlockGetter& level, const BlockPos& p);

// Vanilla WalkNodeEvaluator.getFloorLevel: the top of what stands below p.
double floorLevel(const BlockGetter& level, const BlockPos& p);

} // namespace mc
