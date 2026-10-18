// PathFinder.h -- vanilla A* path finding for walking mobs (PathFinder with
// WalkNodeEvaluator): nodes are the block spaces a mob's feet can be in; steps
// up to a block high, drops up to its fall limit, diagonals past clear corners,
// costs from each space's PathType malus. Pure: reads blocks through a
// BlockGetter and the mob through PathMob.
#pragma once

#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "entity/path/PathTypes.h"

namespace mc {

// What path finding knows of the mob (vanilla Mob as the node evaluator sees it).
struct PathMob {
    glm::dvec3 pos{0.0};
    float width = 0.6f, height = 1.8f;
    float stepHeight = 0.6f; // vanilla maxUpStep
    bool onGround = true, inWater = false;
    PathMalus malus = defaultMaluses();
    bool canFloat = false;    // swims across water (FloatGoal) instead of walking its bottom
    bool canPassDoors = true; // through open doors
    bool canOpenDoors = false;
    int maxFallDistance = 3; // vanilla getMaxFallDistance
    float malusOf(PathType t) const { return malus[size_t(t)]; }
    AABB box() const {
        const double hw = width / 2.0;
        return AABB(pos.x - hw, pos.y, pos.z - hw, pos.x + hw, pos.y + height, pos.z + hw);
    }
    BlockPos blockPos() const { return BlockPos::containing(pos); }
};

struct PathPoint {
    int x = 0, y = 0, z = 0;
    PathType type = PathType::Blocked;
    BlockPos pos() const { return {x, y, z}; }
};

// Vanilla Path: the nodes to walk and the next one to reach.
class Path {
public:
    std::vector<PathPoint> nodes;
    size_t next = 0;
    BlockPos target{0, 0, 0};
    float distToTarget = 0.0f; // Manhattan distance from the last node to the target
    bool reached = false;      // it ends within reach of the target

    bool done() const { return next >= nodes.size(); }
    size_t size() const { return nodes.size(); }
    const PathPoint& nextNode() const { return nodes[next]; }
    BlockPos nextNodePos() const { return nodes[next].pos(); }
    const PathPoint& endNode() const { return nodes.back(); }
    void advance() { ++next; }
    void truncate(size_t length) {
        if (nodes.size() > length) nodes.resize(length);
    }
    // Vanilla getNextEntityPos: the center of the next node for a mob this wide.
    glm::dvec3 nextEntityPos(float width) const {
        const PathPoint& n = nodes[next];
        const double half = double(int(width + 1.0f)) * 0.5;
        return {n.x + half, double(n.y), n.z + half};
    }
    bool sameAs(const Path& o) const;
};

// The best path from the mob toward any of `targets` within `maxRange` blocks walked
// (vanilla PathFinder.findPath): one that ends within `reachRange` (Manhattan) of a
// target, the shortest such; else the one ending closest. Searches at most
// `maxVisitedNodes` nodes (vanilla: the follow range x 16). None if the mob can't start.
std::optional<Path> findPath(const BlockGetter& level, const PathMob& mob, std::span<const BlockPos> targets,
                             float maxRange, int reachRange, int maxVisitedNodes);

// Vanilla WalkNodeEvaluator.getPathType for the mob: the worst type over the spaces its
// box would fill with its feet at `p`.
PathType mobPathType(const BlockGetter& level, const PathMob& mob, const BlockPos& p);

} // namespace mc
