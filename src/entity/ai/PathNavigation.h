// PathNavigation.h -- a walking mob following paths (vanilla GroundPathNavigation):
// finds a path to a block or entity, steers the move control from node to node
// (cutting corners where it can), and gives up when stuck or too slow.
#pragma once

#include <cstdint>
#include <optional>

#include <glm/glm.hpp>

#include "entity/path/PathFinder.h"

namespace mc {

class Entity;
class Level;
class Mob;

class PathNavigation {
public:
    explicit PathNavigation(Mob& mob) : mob_(mob) {}

    // Vanilla GroundPathNavigation.createPath: a path toward `target` (moved down to the
    // ground under it, or up out of a solid block), ending within `reach`.
    std::optional<Path> createPath(Level& level, BlockPos target, int reach);
    std::optional<Path> createPath(Level& level, const Entity& target, int reach);
    // Follow a path (false when there is none or it is already walked).
    bool moveTo(Level& level, std::optional<Path> path, double speed);
    bool moveTo(Level& level, const glm::dvec3& target, double speed);
    bool moveTo(Level& level, const Entity& target, double speed);
    void tick(Level& level);
    bool isDone() const { return !path_ || path_->done(); }
    void stop() { path_.reset(); }
    const std::optional<Path>& path() const { return path_; }
    bool stuck() const { return stuck_; }
    // Vanilla isStableDestination: the block below is solid.
    bool isStableDestination(const Level& level, const BlockPos& p) const;

    bool canFloat = false; // swims (set by FloatGoal)
    bool avoidSun = false; // paths stop before the open sky (skeletons by day)
    // Vanilla WallClimberNavigation (spiders): with no path to an entity, head straight for it.
    bool wallClimber = false;

private:
    void followThePath(Level& level);
    bool shouldTargetNextNodeInDirection(const glm::dvec3& here) const;
    void doStuckDetection(Level& level, const glm::dvec3& here);
    void trimPath(Level& level);
    glm::dvec3 tempMobPos(const Level& level) const;

    Mob& mob_;
    std::optional<Path> path_;
    double speedModifier_ = 0.0;
    int tick_ = 0, lastStuckCheck_ = 0;
    glm::dvec3 lastStuckCheckPos_{0.0};
    BlockPos timeoutCachedNode_{0, 0, 0};
    int64_t timeoutTimer_ = 0, lastTimeoutCheck_ = 0;
    double timeoutLimit_ = 0.0;
    float maxDistanceToWaypoint_ = 0.5f;
    std::optional<BlockPos> targetPos_;
    std::optional<BlockPos> climbTo_; // WallClimberNavigation.pathToPosition
    int reachRange_ = 0;
    bool stuck_ = false;
};

} // namespace mc
