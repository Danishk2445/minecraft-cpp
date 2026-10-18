#include "world/level/Raycast.h"

#include <cmath>

#include "world/chunk/Chunk.h"

namespace mc {

std::optional<std::pair<double, Direction>> clipBox(const AABB& box, const glm::dvec3& from, const glm::dvec3& dir,
                                                     double maxT) {
    double tEnter = 0.0, tExit = maxT;
    Direction face = Direction::Up;
    bool entered = false;
    for (int a = 0; a < 3; ++a) {
        if (std::abs(dir[a]) < 1e-12) {
            if (from[a] < box.min[a] || from[a] > box.max[a]) return std::nullopt;
            continue;
        }
        const double inv = 1.0 / dir[a];
        double t0 = (box.min[a] - from[a]) * inv, t1 = (box.max[a] - from[a]) * inv;
        // Entering through the min side means travelling in +axis, so the face is the negative one.
        Direction near = a == 0 ? Direction::West : a == 1 ? Direction::Down : Direction::North;
        if (t0 > t1) {
            std::swap(t0, t1);
            near = opposite(near);
        }
        if (t0 > tEnter) {
            tEnter = t0;
            face = near;
            entered = true;
        }
        tExit = std::min(tExit, t1);
        if (tEnter > tExit) return std::nullopt;
    }
    if (!entered) {
        // Starting inside the box: report the face behind the ray.
        const glm::dvec3 ad = glm::abs(dir);
        const int a = ad.x >= ad.y && ad.x >= ad.z ? 0 : (ad.y >= ad.z ? 1 : 2);
        static constexpr Direction neg[3] = {Direction::West, Direction::Down, Direction::North};
        face = dir[a] > 0 ? neg[a] : opposite(neg[a]);
    }
    return std::pair{tEnter, face};
}

namespace {

// TransparentBlock (glass) and IronBarsBlock (panes, bars) have empty visual shapes.
bool emptyVisualShape(const Block& b) {
    if (b.className == "pane") return true;
    const std::string_view id = b.id;
    return id == "minecraft:glass" || id == "minecraft:tinted_glass" || id.ends_with("_stained_glass");
}

} // namespace

std::optional<BlockHit> raycastBlocks(const BlockGetter& level, const glm::dvec3& from, const glm::dvec3& dir,
                                      double length, RayFluid fluids, RayShape shapeKind) {
    const BlockRegistry& reg = level.registry();
    // Amanatides-Woo traversal of the blocks the ray passes through.
    glm::ivec3 cell(int(std::floor(from.x)), int(std::floor(from.y)), int(std::floor(from.z)));
    glm::ivec3 step;
    glm::dvec3 tMax, tDelta;
    for (int a = 0; a < 3; ++a) {
        step[a] = dir[a] > 0 ? 1 : (dir[a] < 0 ? -1 : 0);
        tDelta[a] = step[a] != 0 ? std::abs(1.0 / dir[a]) : 1e30;
        const double next = step[a] > 0 ? cell[a] + 1.0 - from[a] : from[a] - cell[a];
        tMax[a] = step[a] != 0 ? next * tDelta[a] : 1e30;
    }
    for (int i = 0; i < 1000; ++i) {
        const BlockPos p(cell);
        const StateId s = level.getBlock(p);
        const StateInfo& info = reg.info(s);
        std::optional<BlockHit> best;
        auto consider = [&](const AABB& b) {
            if (auto hit = clipBox(b, from, dir, length); hit && (!best || hit->first < best->distance))
                best = BlockHit{p, hit->second, from + dir * hit->first, hit->first};
        };
        if (!(info.flags & kAir)) {
            const glm::dvec3 o = glm::dvec3(p.vec()) + reg.shapeOffset(s, p);
            if (shapeKind == RayShape::Outline) {
                for (const AABB& b : reg.outlineShape(s)) consider(b.moved(o));
            } else if (const Shape& shape = reg.collisionShape(s);
                       !shape.empty() && (shapeKind == RayShape::Collider || !emptyVisualShape(reg.blockOf(s)))) {
                for (const AABB& b : shape) consider(b.moved(o));
            }
        }
        if (fluids != RayFluid::None && !info.fluid.empty() && (fluids == RayFluid::Any || info.fluid.source())) {
            const double h = level.fluid(p.above()).kind == info.fluid.kind
                                 ? 1.0
                                 : double(info.fluid.falling ? 8 : 8 - info.fluid.level) / 9.0;
            consider(AABB(p.x, p.y, p.z, p.x + 1.0, p.y + h, p.z + 1.0));
        }
        if (best) return best;
        // Next cell along the axis whose boundary comes first.
        const int a = tMax.x < tMax.y ? (tMax.x < tMax.z ? 0 : 2) : (tMax.y < tMax.z ? 1 : 2);
        if (tMax[a] > length) return std::nullopt;
        cell[a] += step[a];
        tMax[a] += tDelta[a];
        if (cell.y < kWorldMinY - 1 || cell.y > kWorldMaxY + 1) return std::nullopt;
    }
    return std::nullopt;
}

double cameraDistance(const BlockGetter& level, const glm::dvec3& eye, const glm::dvec3& forward, double maxZoom) {
    double zoom = maxZoom;
    for (int i = 0; i < 8; ++i) {
        const glm::dvec3 corner((i & 1) * 2 - 1, (i >> 1 & 1) * 2 - 1, (i >> 2 & 1) * 2 - 1);
        const glm::dvec3 from = eye + corner * 0.1;
        // Each ray reaches as far as the zoom found so far; hits closer to the eye shorten it.
        if (auto hit = raycastBlocks(level, from, -forward, zoom, RayFluid::None, RayShape::Visual)) {
            const glm::dvec3 d = hit->point - eye;
            if (const double distSq = glm::dot(d, d); distSq < zoom * zoom) zoom = std::sqrt(distSq);
        }
    }
    return zoom;
}

} // namespace mc
