#include "entity/Physics.h"

#include <cmath>

namespace mc {
namespace {

constexpr double kEpsilon = 1.0e-7;

double horizontalLengthSq(const glm::dvec3& v) { return v.x * v.x + v.z * v.z; }

} // namespace

double collideAxis(int axis, const AABB& box, const std::vector<AABB>& shapes, double d) {
    if (std::abs(d) < kEpsilon) return 0.0;
    const int a1 = (axis + 1) % 3, a2 = (axis + 2) % 3;
    for (const AABB& s : shapes) {
        // Only shapes overlapping the box across the other two axes can block it.
        if (s.max[a1] <= box.min[a1] + kEpsilon || s.min[a1] >= box.max[a1] - kEpsilon) continue;
        if (s.max[a2] <= box.min[a2] + kEpsilon || s.min[a2] >= box.max[a2] - kEpsilon) continue;
        if (d > 0.0 && s.min[axis] >= box.max[axis] - kEpsilon) d = std::min(d, s.min[axis] - box.max[axis]);
        else if (d < 0.0 && s.max[axis] <= box.min[axis] + kEpsilon) d = std::max(d, s.max[axis] - box.min[axis]);
        if (std::abs(d) < kEpsilon) return 0.0;
    }
    return d;
}

void gatherBlockCollisions(const BlockGetter& level, const AABB& region, std::vector<AABB>& out) {
    const BlockRegistry& reg = level.registry();
    // One block of margin below: fences and walls reach 1.5 blocks up.
    const int x0 = int(std::floor(region.min.x - kEpsilon)) - 1, x1 = int(std::floor(region.max.x + kEpsilon)) + 1;
    const int y0 = int(std::floor(region.min.y - kEpsilon)) - 1, y1 = int(std::floor(region.max.y + kEpsilon)) + 1;
    const int z0 = int(std::floor(region.min.z - kEpsilon)) - 1, z1 = int(std::floor(region.max.z + kEpsilon)) + 1;
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                const StateId s = level.getBlock({x, y, z});
                if (!(reg.info(s).flags & kHasCollision)) continue;
                const glm::dvec3 o = glm::dvec3(x, y, z) + reg.shapeOffset(s, {x, y, z});
                for (const AABB& b : reg.collisionShape(s)) {
                    const AABB w = b.moved(o);
                    if (w.intersects(region)) out.push_back(w);
                }
            }
}

glm::dvec3 collideBoundingBox(const BlockGetter& level, const AABB& box, const glm::dvec3& delta) {
    std::vector<AABB> shapes;
    gatherBlockCollisions(level, box.expandedTowards(delta).inflated(kEpsilon, kEpsilon, kEpsilon), shapes);
    glm::dvec3 out(0.0);
    AABB b = box;
    if (delta.y != 0.0) {
        out.y = collideAxis(1, b, shapes, delta.y);
        if (out.y != 0.0) b = b.moved({0.0, out.y, 0.0});
    }
    const bool zFirst = std::abs(delta.x) < std::abs(delta.z);
    if (zFirst && delta.z != 0.0) {
        out.z = collideAxis(2, b, shapes, delta.z);
        if (out.z != 0.0) b = b.moved({0.0, 0.0, out.z});
    }
    if (delta.x != 0.0) {
        out.x = collideAxis(0, b, shapes, delta.x);
        if (!zFirst && out.x != 0.0) b = b.moved({out.x, 0.0, 0.0});
    }
    if (!zFirst && delta.z != 0.0) out.z = collideAxis(2, b, shapes, delta.z);
    return out;
}

glm::dvec3 collideWithStep(const BlockGetter& level, const AABB& box, const glm::dvec3& delta, double stepHeight,
                           bool onGround) {
    const glm::dvec3 moved = collideBoundingBox(level, box, delta);
    const bool xBlocked = delta.x != moved.x, zBlocked = delta.z != moved.z, yBlocked = delta.y != moved.y;
    const bool landing = onGround || (yBlocked && delta.y < 0.0);
    if (stepHeight <= 0.0 || !landing || !(xBlocked || zBlocked)) return moved;
    // Up by the step height, then across; or across from a box lifted as far as it fits.
    glm::dvec3 best = collideBoundingBox(level, box, {delta.x, stepHeight, delta.z});
    const glm::dvec3 up = collideBoundingBox(level, box.expandedTowards({delta.x, 0.0, delta.z}), {0.0, stepHeight, 0.0});
    if (up.y < stepHeight) {
        const glm::dvec3 across = collideBoundingBox(level, box.moved(up), {delta.x, 0.0, delta.z}) + up;
        if (horizontalLengthSq(across) > horizontalLengthSq(best)) best = across;
    }
    if (horizontalLengthSq(best) > horizontalLengthSq(moved))
        return best + collideBoundingBox(level, box.moved(best), {0.0, -best.y + delta.y, 0.0});
    return moved;
}

bool noCollision(const BlockGetter& level, const AABB& box) {
    std::vector<AABB> shapes;
    gatherBlockCollisions(level, box.inflated(-kEpsilon, -kEpsilon, -kEpsilon), shapes);
    return shapes.empty();
}

} // namespace mc
