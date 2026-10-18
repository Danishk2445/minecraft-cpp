#include "entity/ai/PathNavigation.h"

#include <cmath>

#include "entity/mob/Mob.h"
#include "world/level/Level.h"

namespace mc {

std::optional<Path> PathNavigation::createPath(Level& level, BlockPos target, int reach) {
    if (!level.chunks().isReady(ChunkPos::of(target))) return std::nullopt;
    const BlockRegistry& reg = level.registry();
    auto isAir = [&](const BlockPos& p) { return (reg.info(level.getBlock(p)).flags & kAir) != 0; };
    auto isSolid = [&](const BlockPos& p) { return pathBlockInfo(reg, level.getBlock(p)).solid; };
    // A target in the air moves down to the ground (or up to the first block above).
    if (isAir(target)) {
        BlockPos m = target.offset(0, -1, 0);
        while (m.y >= kWorldMinY && isAir(m)) m.y -= 1;
        if (m.y >= kWorldMinY) target = m.offset(0, 1, 0);
        else {
            m.y = target.y + 1;
            while (m.y < kWorldMaxY && isAir(m)) m.y += 1;
            target = m;
        }
    }
    // One inside something solid moves up out of it.
    if (isSolid(target)) {
        BlockPos m = target.offset(0, 1, 0);
        while (m.y < kWorldMaxY && isSolid(m)) m.y += 1;
        target = m;
    }
    // Vanilla PathNavigation.createPath.
    if (mob_.pos.y < double(kWorldMinY)) return std::nullopt;
    if (!(mob_.onGround || mob_.inWater || mob_.inLava)) return std::nullopt; // canUpdatePath
    if (path_ && !path_->done() && targetPos_ && *targetPos_ == target) return path_;
    const BlockPos targets[1] = {target};
    std::optional<Path> path = findPath(level, mob_.pathMob(level), targets, mob_.followRange, reach,
                                        int(std::floor(mob_.followRange * 16.0f)));
    if (path) {
        targetPos_ = path->target;
        reachRange_ = reach;
        timeoutCachedNode_ = {0, 0, 0};
        timeoutTimer_ = 0;
        timeoutLimit_ = 0.0;
        stuck_ = false;
    }
    return path;
}

std::optional<Path> PathNavigation::createPath(Level& level, const Entity& target, int reach) {
    return createPath(level, BlockPos::containing(target.pos), reach);
}

bool PathNavigation::moveTo(Level& level, std::optional<Path> path, double speed) {
    if (!path) {
        path_.reset();
        return false;
    }
    if (!path_ || !path->sameAs(*path_)) path_ = std::move(path);
    if (isDone()) return false;
    trimPath(level);
    if (path_->size() == 0) return false;
    speedModifier_ = speed;
    lastStuckCheck_ = tick_;
    lastStuckCheckPos_ = tempMobPos(level);
    return true;
}

bool PathNavigation::moveTo(Level& level, const glm::dvec3& target, double speed) {
    return moveTo(level, createPath(level, BlockPos::containing(target), 1), speed);
}

bool PathNavigation::moveTo(Level& level, const Entity& target, double speed) {
    std::optional<Path> path = createPath(level, target, wallClimber ? 0 : 1);
    if (wallClimber) {
        climbTo_ = BlockPos::containing(target.pos);
        if (!path) {
            speedModifier_ = speed;
            return true;
        }
    }
    return path && moveTo(level, std::move(path), speed);
}

glm::dvec3 PathNavigation::tempMobPos(const Level& level) const {
    // Vanilla GroundPathNavigation.getSurfaceY: in water, up to the surface it floats on.
    if (!mob_.inWater || !canFloat) return {mob_.pos.x, std::floor(mob_.pos.y + 0.5), mob_.pos.z};
    int y = int(std::floor(mob_.pos.y));
    for (int k = 0; k < 16 && level.fluid({int(std::floor(mob_.pos.x)), y, int(std::floor(mob_.pos.z))}).kind == FluidKind::Water;
         ++k)
        ++y;
    return {mob_.pos.x, double(y), mob_.pos.z};
}

bool PathNavigation::isStableDestination(const Level& level, const BlockPos& p) const {
    return (level.registry().info(level.getBlock(p.offset(0, -1, 0))).flags & kOpaqueCube) != 0;
}

void PathNavigation::trimPath(Level& level) {
    if (!path_) return;
    // Vanilla GroundPathNavigation.trimPath with avoidSun: the path stops at the open sky.
    if (avoidSun) {
        if (level.canSeeSky(BlockPos::containing({mob_.pos.x, mob_.pos.y + 0.5, mob_.pos.z}))) return;
        for (size_t i = 0; i < path_->size(); ++i)
            if (level.canSeeSky(path_->nodes[i].pos())) {
                path_->truncate(i);
                return;
            }
    }
}

void PathNavigation::tick(Level& level) {
    ++tick_;
    if (isDone() && wallClimber && climbTo_) {
        // Straight at it, until within a body's width (at its height or above it).
        const glm::dvec3 c(climbTo_->x + 0.5, climbTo_->y + 0.5, climbTo_->z + 0.5);
        const glm::dvec3 above(climbTo_->x + 0.5, std::floor(mob_.pos.y) + 0.5, climbTo_->z + 0.5);
        const double w = mob_.width;
        if (glm::length(c - mob_.pos) >= w && (!(mob_.pos.y > double(climbTo_->y)) || glm::length(above - mob_.pos) >= w))
            mob_.moveControl.setWantedPosition(glm::dvec3(climbTo_->x, climbTo_->y, climbTo_->z), speedModifier_);
        else
            climbTo_.reset();
        return;
    }
    if (isDone()) return;
    if (mob_.onGround || mob_.inWater || mob_.inLava) {
        followThePath(level);
    } else {
        // Falling past the next node's column: it counts as reached.
        const glm::dvec3 here = tempMobPos(level), next = path_->nextEntityPos(mob_.width);
        if (here.y > next.y && !mob_.onGround && std::floor(here.x) == std::floor(next.x) &&
            std::floor(here.z) == std::floor(next.z))
            path_->advance();
    }
    if (isDone()) return;
    // Steer toward the next node, at its floor.
    const glm::dvec3 next = path_->nextEntityPos(mob_.width);
    const BlockPos at = BlockPos::containing(next);
    const double y = (level.registry().info(level.getBlock(at.offset(0, -1, 0))).flags & kAir) ? next.y
                                                                                                : floorLevel(level, at);
    mob_.moveControl.setWantedPosition({next.x, y, next.z}, speedModifier_);
}

bool PathNavigation::shouldTargetNextNodeInDirection(const glm::dvec3& here) const {
    if (path_->next + 1 >= path_->size()) return false;
    const BlockPos n = path_->nextNodePos();
    const glm::dvec3 nextCenter(n.x + 0.5, n.y, n.z + 0.5);
    if (glm::length(nextCenter - here) >= 2.0) return false;
    const BlockPos a = path_->nodes[path_->next + 1].pos();
    const glm::dvec3 afterCenter(a.x + 0.5, a.y, a.z + 0.5);
    const glm::dvec3 toNext = nextCenter - here, toAfter = afterCenter - here;
    const double dn = glm::dot(toNext, toNext), da = glm::dot(toAfter, toAfter);
    if (!(da < dn) && !(dn < 0.5)) return false;
    return glm::dot(glm::normalize(toAfter), glm::normalize(toNext)) < 0.0;
}

void PathNavigation::followThePath(Level& level) {
    const glm::dvec3 here = tempMobPos(level);
    maxDistanceToWaypoint_ = mob_.width > 0.75f ? mob_.width / 2.0f : 0.75f - mob_.width / 2.0f;
    const BlockPos n = path_->nextNodePos();
    const double dx = std::abs(mob_.pos.x - (n.x + 0.5)), dy = std::abs(mob_.pos.y - n.y),
                 dz = std::abs(mob_.pos.z - (n.z + 0.5));
    const bool reached = dx < maxDistanceToWaypoint_ && dz < maxDistanceToWaypoint_ && dy < 1.0;
    const PathType t = path_->nextNode().type;
    const bool cutCorner = t != PathType::DangerFire && t != PathType::DangerOther && t != PathType::WalkableDoor;
    if (reached || (cutCorner && shouldTargetNextNodeInDirection(here))) path_->advance();
    doStuckDetection(level, here);
}

void PathNavigation::doStuckDetection(Level& level, const glm::dvec3& here) {
    if (tick_ - lastStuckCheck_ > 100) {
        const float speed = mob_.speed >= 1.0f ? mob_.speed : mob_.speed * mob_.speed;
        const float expected = speed * 100.0f * 0.25f;
        const glm::dvec3 d = here - lastStuckCheckPos_;
        if (glm::dot(d, d) < double(expected * expected)) {
            stuck_ = true;
            stop();
        } else {
            stuck_ = false;
        }
        lastStuckCheck_ = tick_;
        lastStuckCheckPos_ = here;
    }
    if (!path_ || path_->done()) return;
    // A node it takes three times too long to reach: give up on the path (vanilla timeoutPath).
    const BlockPos node = path_->nextNodePos();
    const int64_t now = level.gameTime();
    if (node == timeoutCachedNode_) {
        timeoutTimer_ += now - lastTimeoutCheck_;
    } else {
        timeoutCachedNode_ = node;
        const double distance = glm::length(here - glm::dvec3(node.x + 0.5, node.y, node.z + 0.5));
        timeoutLimit_ = mob_.speed > 0.0f ? distance / double(mob_.speed) * 20.0 : 0.0;
    }
    if (timeoutLimit_ > 0.0 && double(timeoutTimer_) > timeoutLimit_ * 3.0) {
        timeoutCachedNode_ = {0, 0, 0};
        timeoutTimer_ = 0;
        timeoutLimit_ = 0.0;
        stop();
    }
    lastTimeoutCheck_ = now;
}

} // namespace mc
