#include "entity/path/PathFinder.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <memory>
#include <unordered_map>

#include "entity/Physics.h"
#include "resources/ResourcePack.h"
#include "world/block/BlockGetter.h"
#include "world/chunk/Chunk.h"

namespace mc {
namespace {

uint64_t nodeKey(int x, int y, int z) {
    return (uint64_t(uint32_t(x)) & 0x3FFFFFF) | ((uint64_t(uint32_t(z)) & 0x3FFFFFF) << 26) |
           ((uint64_t(uint32_t(y)) & 0xFFF) << 52);
}

struct Node {
    int x, y, z;
    int heapIdx = -1;
    float g = 0.0f, h = 0.0f, f = 0.0f;
    Node* cameFrom = nullptr;
    bool closed = false;
    float walkedDistance = 0.0f;
    float costMalus = 0.0f;
    PathType type = PathType::Blocked;

    Node(int x_, int y_, int z_) : x(x_), y(y_), z(z_) {}
    bool inOpenSet() const { return heapIdx >= 0; }
    float distanceTo(int ox, int oy, int oz) const {
        const float dx = float(ox - x), dy = float(oy - y), dz = float(oz - z);
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    float distanceTo(const Node& o) const { return distanceTo(o.x, o.y, o.z); }
    float distanceManhattan(int ox, int oy, int oz) const {
        return float(std::abs(ox - x) + std::abs(oy - y) + std::abs(oz - z));
    }
};

// Vanilla BinaryHeap: a min-heap of nodes by f that knows each node's index.
class OpenSet {
public:
    void insert(Node* n) {
        n->heapIdx = int(heap_.size());
        heap_.push_back(n);
        up(n->heapIdx);
    }
    Node* pop() {
        Node* top = heap_.front();
        heap_.front() = heap_.back();
        heap_.front()->heapIdx = 0;
        heap_.pop_back();
        if (!heap_.empty()) down(0);
        top->heapIdx = -1;
        return top;
    }
    void changeCost(Node* n, float cost) {
        const float old = n->f;
        n->f = cost;
        if (cost < old) up(n->heapIdx);
        else down(n->heapIdx);
    }
    bool empty() const { return heap_.empty(); }

private:
    void up(int i) {
        Node* n = heap_[size_t(i)];
        while (i > 0) {
            const int parent = (i - 1) >> 1;
            Node* p = heap_[size_t(parent)];
            if (!(n->f < p->f)) break;
            heap_[size_t(i)] = p;
            p->heapIdx = i;
            i = parent;
        }
        heap_[size_t(i)] = n;
        n->heapIdx = i;
    }
    void down(int i) {
        Node* n = heap_[size_t(i)];
        const float f = n->f;
        const int size = int(heap_.size());
        while (true) {
            const int l = 1 + (i << 1), r = l + 1;
            if (l >= size) break;
            Node* ln = heap_[size_t(l)];
            const float lf = ln->f;
            Node* rn = r >= size ? nullptr : heap_[size_t(r)];
            const float rf = rn ? rn->f : std::numeric_limits<float>::infinity();
            if (lf < rf) {
                if (lf >= f) break;
                heap_[size_t(i)] = ln;
                ln->heapIdx = i;
                i = l;
            } else {
                if (rf >= f) break;
                heap_[size_t(i)] = rn;
                rn->heapIdx = i;
                i = r;
            }
        }
        heap_[size_t(i)] = n;
        n->heapIdx = i;
    }
    std::vector<Node*> heap_;
};

// Vanilla WalkNodeEvaluator for one search.
class Walker {
public:
    Walker(const BlockGetter& level, const PathMob& mob)
        : level_(level), reg_(level.registry()), mob_(mob) {}

    Node* node(int x, int y, int z) {
        auto [it, inserted] = nodes_.try_emplace(nodeKey(x, y, z), nullptr);
        if (inserted) it->second = &pool_.emplace_back(x, y, z);
        return it->second;
    }

    PathType cachedType(int x, int y, int z) {
        auto [it, inserted] = types_.try_emplace(nodeKey(x, y, z), PathType::Blocked);
        if (inserted) it->second = mobPathType(level_, mob_, {x, y, z});
        return it->second;
    }

    Node* start() {
        const BlockPos feet = mob_.blockPos();
        int y = feet.y;
        auto stateAt = [&](double x, double yy, double z) { return level_.getBlock(BlockPos::containing({x, yy, z})); };
        if (mob_.canFloat && mob_.inWater) {
            // Up to the water's surface.
            StateId s = stateAt(mob_.pos.x, y, mob_.pos.z);
            while (true) {
                const FluidState f = reg_.info(s).fluid;
                if (!(f.kind == FluidKind::Water && (shortId(reg_.blockOf(s).id) == "water" || f.source()))) {
                    --y;
                    break;
                }
                ++y;
                s = stateAt(mob_.pos.x, y, mob_.pos.z);
            }
        } else if (mob_.onGround) {
            y = int(std::floor(mob_.pos.y + 0.5));
        } else {
            // Falling: down to the first thing it could stand on.
            BlockPos m = BlockPos::containing({mob_.pos.x, mob_.pos.y + 1.0, mob_.pos.z});
            while (m.y > kWorldMinY) {
                y = m.y;
                m.y -= 1;
                const StateId s = level_.getBlock(m);
                if (!(reg_.info(s).flags & kAir) && !pathBlockInfo(reg_, s).pathfindable) break;
            }
        }
        auto canStartAt = [&](int x, int z) {
            const PathType t = cachedType(x, y, z);
            return t != PathType::Open && mob_.malusOf(t) >= 0.0f;
        };
        if (!canStartAt(feet.x, feet.z)) {
            const AABB b = mob_.box();
            for (const auto& [cx, cz] : {std::pair{b.min.x, b.min.z}, std::pair{b.min.x, b.max.z}, std::pair{b.max.x, b.min.z},
                                         std::pair{b.max.x, b.max.z}}) {
                const int x = int(std::floor(cx)), z = int(std::floor(cz));
                if (canStartAt(x, z)) return startNode(x, y, z);
            }
        }
        return startNode(feet.x, y, feet.z);
    }

    // Vanilla getNeighbors: the four sides, then the diagonals between accepted sides.
    int neighbors(Node** out, Node* n) {
        int count = 0;
        int stepLimit = 0;
        const PathType above = cachedType(n->x, n->y + 1, n->z);
        const PathType here = cachedType(n->x, n->y, n->z);
        if (mob_.malusOf(above) >= 0.0f && here != PathType::StickyHoney)
            stepLimit = int(std::floor(std::max(1.0f, mob_.stepHeight)));
        const double floor = floorAt(n->x, n->y, n->z);
        // Direction.Plane.HORIZONTAL order: north, east, south, west (2D data values 2, 3, 0, 1).
        static constexpr int kDx[4] = {0, 1, 0, -1}, kDz[4] = {-1, 0, 1, 0};
        Node* side[4] = {};
        for (int d = 0; d < 4; ++d) {
            Node* m = acceptedNode(n->x + kDx[d], n->y, n->z + kDz[d], stepLimit, floor, kDx[d], kDz[d], here);
            side[d] = m;
            if (m && !m->closed && (m->costMalus >= 0.0f || n->costMalus < 0.0f)) out[count++] = m;
        }
        for (int d = 0; d < 4; ++d) {
            const int cw = (d + 1) & 3; // clockwise of north is east
            if (!diagonalValid(n, side[d], side[cw])) continue;
            Node* m = acceptedNode(n->x + kDx[d] + kDx[cw], n->y, n->z + kDz[d] + kDz[cw], stepLimit, floor, kDx[d], kDz[d],
                                   here);
            if (m && !m->closed && m->type != PathType::WalkableDoor && m->costMalus >= 0.0f) out[count++] = m;
        }
        return count;
    }

private:
    Node* startNode(int x, int y, int z) {
        Node* n = node(x, y, z);
        n->type = cachedType(x, y, z);
        n->costMalus = mob_.malusOf(n->type);
        return n;
    }

    double floorAt(int x, int y, int z) const {
        const BlockPos p{x, y, z};
        if (mob_.canFloat && level_.fluid(p).kind == FluidKind::Water) return double(y) + 0.5;
        return floorLevel(level_, p);
    }

    double jumpHeight() const { return std::max(1.125, double(mob_.stepHeight)); }

    static bool partialCollision(PathType t) {
        return t == PathType::Fence || t == PathType::DoorWoodClosed || t == PathType::DoorIronClosed;
    }

    bool diagonalValid(const Node* root, const Node* xNode, const Node* zNode) const {
        if (!zNode || !xNode || zNode->y > root->y || xNode->y > root->y) return false;
        if (xNode->type == PathType::WalkableDoor || zNode->type == PathType::WalkableDoor) return false;
        const bool thinFences = zNode->type == PathType::Fence && xNode->type == PathType::Fence && mob_.width < 0.5f;
        return (zNode->y < root->y || zNode->costMalus >= 0.0f || thinFences) &&
               (xNode->y < root->y || xNode->costMalus >= 0.0f || thinFences);
    }

    Node* typedNode(int x, int y, int z, PathType type, float malus) {
        Node* n = node(x, y, z);
        n->type = type;
        n->costMalus = std::max(n->costMalus, malus);
        return n;
    }
    Node* blockedNode(int x, int y, int z) {
        Node* n = node(x, y, z);
        n->type = PathType::Blocked;
        n->costMalus = -1.0f;
        return n;
    }

    bool hasCollisions(const AABB& box) const { return !noCollision(level_, box); }

    bool canReachWithoutCollision(const Node* n) const {
        AABB b = mob_.box();
        const glm::dvec3 size = b.max - b.min;
        glm::dvec3 v(double(n->x) - mob_.pos.x + size.x / 2.0, double(n->y) - mob_.pos.y + size.y / 2.0,
                     double(n->z) - mob_.pos.z + size.z / 2.0);
        const double avg = (size.x + size.y + size.z) / 3.0;
        const int steps = int(std::ceil(glm::length(v) / avg));
        v /= double(steps);
        for (int i = 1; i <= steps; ++i) {
            b = b.moved(v);
            if (hasCollisions(b)) return false;
        }
        return true;
    }

    // Vanilla findAcceptedNode: the space at (x, y, z) if the mob can be there, else one a
    // step up, the first ground below a drop, or the bottom of water.
    Node* acceptedNode(int x, int y, int z, int stepLimit, double nodeFloor, int dx, int dz, PathType fromType) {
        const double floor = floorAt(x, y, z);
        if (floor - nodeFloor > jumpHeight()) return nullptr;
        const PathType type = cachedType(x, y, z);
        const float malus = mob_.malusOf(type);
        Node* n = malus >= 0.0f ? typedNode(x, y, z, type, malus) : nullptr;
        if (partialCollision(fromType) && n && n->costMalus >= 0.0f && !canReachWithoutCollision(n)) n = nullptr;
        if (type == PathType::Walkable) return n;
        if ((!n || n->costMalus < 0.0f) && stepLimit > 0 && type != PathType::Fence && type != PathType::UnpassableRail &&
            type != PathType::Trapdoor && type != PathType::PowderSnow) {
            return tryJumpOn(x, y, z, stepLimit, nodeFloor, dx, dz, fromType);
        }
        if (type == PathType::Water && !mob_.canFloat) return firstNonWaterBelow(x, y, z, n);
        if (type == PathType::Open) return firstGroundBelow(x, y, z);
        if (partialCollision(type) && !n) {
            Node* closed = node(x, y, z);
            closed->closed = true;
            closed->type = type;
            closed->costMalus = defaultMalus(type);
            return closed;
        }
        return n;
    }

    Node* tryJumpOn(int x, int y, int z, int stepLimit, double nodeFloor, int dx, int dz, PathType fromType) {
        Node* n = acceptedNode(x, y + 1, z, stepLimit - 1, nodeFloor, dx, dz, fromType);
        if (!n) return nullptr;
        if (mob_.width >= 1.0f) return n;
        if (n->type != PathType::Open && n->type != PathType::Walkable) return n;
        // The mob's box rising from the space it comes from must fit.
        const double cx = double(x - dx) + 0.5, cz = double(z - dz) + 0.5, hw = double(mob_.width) / 2.0;
        const double bottom = floorAt(int(std::floor(cx)), y + 1, int(std::floor(cz))) + 0.001;
        const double top = double(mob_.height) + floorAt(n->x, n->y, n->z) - 0.002;
        const AABB box(cx - hw, bottom, cz - hw, cx + hw, top, cz + hw);
        return hasCollisions(box) ? nullptr : n;
    }

    Node* firstNonWaterBelow(int x, int y, int z, Node* n) {
        for (--y; y > kWorldMinY; --y) {
            const PathType t = cachedType(x, y, z);
            if (t != PathType::Water) return n;
            n = typedNode(x, y, z, t, mob_.malusOf(t));
        }
        return n;
    }

    Node* firstGroundBelow(int x, int y, int z) {
        for (int i = y - 1; i >= kWorldMinY; --i) {
            if (y - i > mob_.maxFallDistance) return blockedNode(x, i, z);
            const PathType t = cachedType(x, i, z);
            const float malus = mob_.malusOf(t);
            if (t != PathType::Open) return malus >= 0.0f ? typedNode(x, i, z, t, malus) : blockedNode(x, i, z);
        }
        return blockedNode(x, y, z);
    }

    const BlockGetter& level_;
    const BlockRegistry& reg_;
    const PathMob& mob_;
    std::deque<Node> pool_;
    std::unordered_map<uint64_t, Node*> nodes_;
    std::unordered_map<uint64_t, PathType> types_;
};

struct Target {
    int x, y, z;
    BlockPos pos;
    float bestHeuristic = std::numeric_limits<float>::max();
    Node* best = nullptr;
    bool reached = false;
};

Path reconstruct(Node* end, const BlockPos& target, bool reached) {
    Path path;
    for (Node* n = end; n; n = n->cameFrom) path.nodes.push_back({n->x, n->y, n->z, n->type});
    std::reverse(path.nodes.begin(), path.nodes.end());
    path.target = target;
    path.reached = reached;
    const PathPoint& last = path.nodes.back();
    path.distToTarget = path.nodes.empty() ? std::numeric_limits<float>::max()
                                           : float(std::abs(last.x - target.x) + std::abs(last.y - target.y) +
                                                   std::abs(last.z - target.z));
    return path;
}

} // namespace

bool Path::sameAs(const Path& o) const {
    if (o.nodes.size() != nodes.size()) return false;
    for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].x != o.nodes[i].x || nodes[i].y != o.nodes[i].y || nodes[i].z != o.nodes[i].z) return false;
    return true;
}

PathType mobPathType(const BlockGetter& level, const PathMob& mob, const BlockPos& p) {
    const int w = int(std::floor(mob.width + 1.0f)), h = int(std::floor(mob.height + 1.0f));
    const BlockPos feet = mob.blockPos();
    uint32_t seen = 0;
    for (int i = 0; i < w; ++i)
        for (int j = 0; j < h; ++j)
            for (int k = 0; k < w; ++k) {
                PathType t = pathTypeAt(level, p.offset(i, j, k));
                if (t == PathType::DoorWoodClosed && mob.canOpenDoors && mob.canPassDoors) t = PathType::WalkableDoor;
                if (t == PathType::DoorOpen && !mob.canPassDoors) t = PathType::Blocked;
                if (t == PathType::Rail && pathTypeAt(level, feet) != PathType::Rail &&
                    pathTypeAt(level, feet.offset(0, -1, 0)) != PathType::Rail)
                    t = PathType::UnpassableRail;
                seen |= 1u << uint32_t(t);
            }
    auto has = [&](PathType t) { return (seen >> uint32_t(t)) & 1u; };
    if (has(PathType::Fence)) return PathType::Fence;
    if (has(PathType::UnpassableRail)) return PathType::UnpassableRail;
    PathType best = PathType::Blocked;
    for (size_t i = 0; i < kPathTypeCount; ++i) {
        if (!has(PathType(i))) continue;
        const PathType t = PathType(i);
        if (mob.malusOf(t) < 0.0f) return t;
        if (mob.malusOf(t) >= mob.malusOf(best)) best = t;
    }
    if (w <= 1 && best != PathType::Open && mob.malusOf(best) == 0.0f && pathTypeAt(level, p) == PathType::Open)
        return PathType::Open;
    return best;
}

std::optional<Path> findPath(const BlockGetter& level, const PathMob& mob, std::span<const BlockPos> targetPositions,
                             float maxRange, int reachRange, int maxVisitedNodes) {
    if (targetPositions.empty()) return std::nullopt;
    Walker walker(level, mob);
    Node* start = walker.start();
    if (!start) return std::nullopt;
    std::vector<Target> targets;
    for (const BlockPos& p : targetPositions) targets.push_back({p.x, p.y, p.z, p});
    auto bestH = [&](Node* n) {
        float best = std::numeric_limits<float>::max();
        for (Target& t : targets) {
            const float d = n->distanceTo(t.x, t.y, t.z);
            if (d < t.bestHeuristic) {
                t.bestHeuristic = d;
                t.best = n;
            }
            best = std::min(best, d);
        }
        return best;
    };
    start->g = 0.0f;
    start->h = bestH(start);
    start->f = start->h;
    OpenSet open;
    open.insert(start);
    Node* neighbors[32];
    bool anyReached = false;
    int visited = 0;
    while (!open.empty()) {
        if (++visited >= maxVisitedNodes) break;
        Node* n = open.pop();
        n->closed = true;
        for (Target& t : targets)
            if (n->distanceManhattan(t.x, t.y, t.z) <= float(reachRange)) {
                t.reached = true; // the path still ends at its best node so far, as in vanilla
                anyReached = true;
            }
        if (anyReached) break;
        if (n->distanceTo(*start) >= maxRange) continue;
        const int count = walker.neighbors(neighbors, n);
        for (int i = 0; i < count; ++i) {
            Node* m = neighbors[i];
            const float step = n->distanceTo(*m);
            m->walkedDistance = n->walkedDistance + step;
            const float g = n->g + step + m->costMalus;
            if (m->walkedDistance < maxRange && (!m->inOpenSet() || g < m->g)) {
                m->cameFrom = n;
                m->g = g;
                m->h = bestH(m) * 1.5f;
                if (m->inOpenSet()) {
                    open.changeCost(m, m->g + m->h);
                } else {
                    m->f = m->g + m->h;
                    open.insert(m);
                }
            }
        }
    }
    std::optional<Path> best;
    for (const Target& t : targets) {
        if (!t.best || (anyReached && !t.reached)) continue;
        Path p = reconstruct(t.best, t.pos, t.reached);
        if (!best) best = std::move(p);
        else if (anyReached ? p.size() < best->size()
                            : (p.distToTarget < best->distToTarget ||
                               (p.distToTarget == best->distToTarget && p.size() < best->size())))
            best = std::move(p);
    }
    return best;
}

} // namespace mc
