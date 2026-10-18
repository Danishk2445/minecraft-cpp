#include "world/mesh/VisGraph.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace mc {
namespace {

constexpr int idx(int x, int y, int z) { return x | z << 4 | y << 8; }

constexpr uint16_t kVisited = 1u << 12;

// The boundary faces a cell lies on (one bit per Direction).
uint8_t edgesOf(int i) {
    const int x = i & 15, z = (i >> 4) & 15, y = i >> 8;
    uint8_t m = 0;
    if (x == 0) m |= 1u << int(Direction::West);
    if (x == 15) m |= 1u << int(Direction::East);
    if (y == 0) m |= 1u << int(Direction::Down);
    if (y == 15) m |= 1u << int(Direction::Up);
    if (z == 0) m |= 1u << int(Direction::North);
    if (z == 15) m |= 1u << int(Direction::South);
    return m;
}

} // namespace

VisibilitySet resolveVisibility(const std::bitset<4096>& opaque) {
    const size_t solid = opaque.count();
    if (solid < 256) return VisibilitySet{};
    if (solid == 4096) return VisibilitySet::none();
    VisibilitySet out = VisibilitySet::none();
    std::bitset<4096> seen = opaque;
    std::array<uint16_t, 4096> queue;
    // Every open boundary cell starts a region (vanilla INDEX_OF_EDGES).
    for (int y = 0; y < 16; ++y)
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) {
                if (x != 0 && x != 15 && y != 0 && y != 15 && z != 0 && z != 15) continue;
                const int start = idx(x, y, z);
                if (seen[size_t(start)]) continue;
                seen[size_t(start)] = true;
                uint8_t faces = 0;
                size_t head = 0, tail = 0;
                queue[tail++] = uint16_t(start);
                while (head < tail) {
                    const int i = queue[head++];
                    faces |= edgesOf(i);
                    const int cx = i & 15, cz = (i >> 4) & 15, cy = i >> 8;
                    for (Direction d : kDirections) {
                        const glm::ivec3 n = glm::ivec3(cx, cy, cz) + normal(d);
                        if (n.x < 0 || n.x > 15 || n.y < 0 || n.y > 15 || n.z < 0 || n.z > 15) continue;
                        const int j = idx(n.x, n.y, n.z);
                        if (seen[size_t(j)]) continue;
                        seen[size_t(j)] = true;
                        queue[tail++] = uint16_t(j);
                    }
                }
                for (Direction a : kDirections)
                    if (faces & (1u << int(a)))
                        for (Direction b : kDirections)
                            if (faces & (1u << int(b))) out.set(a, b);
            }
    return out;
}

void SectionOcclusionGraph::search(const glm::dvec3& camera, int renderDistance,
                                   const std::vector<std::pair<ChunkPos, const ColumnVisibility*>>& columns) {
    reached_.clear();
    const int R = renderDistance + 1;
    const int W = 2 * R + 1;
    const BlockPos cb = BlockPos::containing(camera);
    const ChunkPos cc = ChunkPos::of(cb);
    const int camSi = int(std::floor((camera.y - kWorldMinY) / 16.0));
    columns_.assign(size_t(W * W), nullptr);
    inView_.assign(size_t(W * W), 0);
    nodes_.assign(size_t(W * W * kSectionCount), 0);
    queue_.clear();
    const double maxDist = renderDistance * 16.0 + 16.0;
    for (int dz = -R; dz <= R; ++dz)
        for (int dx = -R; dx <= R; ++dx) {
            const double x = (cc.x + dx) * 16.0 + 8.0 - camera.x, z = (cc.z + dz) * 16.0 + 8.0 - camera.z;
            inView_[size_t((dz + R) * W + dx + R)] = x * x + z * z <= maxDist * maxDist;
        }
    for (const auto& [pos, column] : columns) {
        const int dx = pos.x - cc.x, dz = pos.z - cc.z;
        if (std::abs(dx) <= R && std::abs(dz) <= R) columns_[size_t((dz + R) * W + dx + R)] = column;
    }
    const auto nodeIndex = [&](int dx, int dz, int si) { return uint32_t(((dz + R) * W + dx + R) * kSectionCount + si); };
    const auto bit = [](Direction d) { return uint16_t(1u << int(d)); };

    if (camSi >= 0 && camSi < kSectionCount) {
        const uint32_t start = nodeIndex(0, 0, camSi);
        nodes_[start] = kVisited;
        queue_.push_back(start);
    } else {
        // Above or below the world: every column's top (bottom) section within the render
        // distance, nearest first, entered from above (below) and heading outward.
        const bool above = camSi >= kSectionCount;
        const int si = above ? kSectionCount - 1 : 0;
        const Direction enter = above ? Direction::Down : Direction::Up;
        std::vector<std::pair<double, uint32_t>> seeds;
        for (int dz = -R; dz <= R; ++dz)
            for (int dx = -R; dx <= R; ++dx) {
                const size_t col = size_t((dz + R) * W + dx + R);
                if (!inView_[col] || !columns_[col]) continue;
                uint16_t dirs = bit(enter);
                if (dx > 0) dirs |= bit(Direction::East);
                if (dx < 0) dirs |= bit(Direction::West);
                if (dz > 0) dirs |= bit(Direction::South);
                if (dz < 0) dirs |= bit(Direction::North);
                const uint32_t n = nodeIndex(dx, dz, si);
                nodes_[n] = uint16_t(kVisited | bit(enter) | dirs << 6);
                const double x = (cc.x + dx) * 16.0 + 8.0 - camera.x, y = sectionMinY(si) + 8.0 - camera.y,
                             z = (cc.z + dz) * 16.0 + 8.0 - camera.z;
                seeds.emplace_back(x * x + y * y + z * z, n);
            }
        std::sort(seeds.begin(), seeds.end());
        for (const auto& [d, n] : seeds) queue_.push_back(n);
    }

    // Vanilla's line of sight for far sections: from the neighbor's corner toward the camera
    // in steps of a section's diagonal, every section passed must already be reached.
    const glm::dvec3 camCenter(double(cc.x * 16 + 8), double(sectionMinY(std::clamp(camSi, 0, kSectionCount - 1)) + 8),
                               double(cc.z * 16 + 8));
    const auto lineOfSight = [&](int ndx, int ndz, int nsi, Direction d) {
        const glm::dvec3 origin((cc.x + ndx) * 16.0, double(sectionMinY(nsi)), (cc.z + ndz) * 16.0);
        glm::dvec3 corner = origin;
        for (int a = 0; a < 3; ++a) {
            const bool along = int(axisOf(d)) == a;
            const bool offset = along ? camCenter[a] > origin[a] : camCenter[a] < origin[a];
            if (offset) corner[a] += 16.0;
        }
        const glm::dvec3 step = glm::normalize(camera - corner) * 28.0; // ceil(sqrt(3) * 16)
        glm::dvec3 v = corner;
        while (glm::dot(camera - v, camera - v) > 3600.0) {
            v += step;
            if (v.y > kWorldMaxY + 1 || v.y < kWorldMinY) break;
            const BlockPos p = BlockPos::containing(v);
            const int dx = (p.x >> 4) - cc.x, dz = (p.z >> 4) - cc.z, si = sectionIndexOfY(p.y);
            if (std::abs(dx) > R || std::abs(dz) > R || si < 0 || si >= kSectionCount) return false;
            if (!(nodes_[nodeIndex(dx, dz, si)] & kVisited)) return false;
        }
        return true;
    };

    for (size_t head = 0; head < queue_.size(); ++head) {
        const uint32_t idx = queue_[head];
        const int si = int(idx % kSectionCount);
        const int col = int(idx / kSectionCount);
        const int dx = col % W - R, dz = col / W - R;
        const ColumnVisibility* column = columns_[size_t(col)];
        const uint16_t node = nodes_[idx];
        const uint16_t sources = node & 63, dirs = (node >> 6) & 63;
        const VisibilitySet* section = column ? &column->visibility[size_t(si)] : nullptr;
        if (column) reached_.push_back({column, ChunkPos(cc.x + dx, cc.z + dz), si});
        const bool far = std::abs(dx) * 16 > 60 || std::abs(si - camSi) * 16 > 60 || std::abs(dz) * 16 > 60;
        for (Direction d : kDirections) {
            const glm::ivec3 o = normal(d);
            const int ndx = dx + o.x, ndz = dz + o.z, nsi = si + o.y;
            if (nsi < 0 || nsi >= kSectionCount || std::abs(ndx) > R || std::abs(ndz) > R) continue;
            const size_t ncol = size_t((ndz + R) * W + ndx + R);
            if (!inView_[ncol]) continue;
            // Never back the way the path came; only out through a face that one of the
            // faces it came in by can see.
            if (dirs & bit(opposite(d))) continue;
            if (sources && section) {
                bool sees = false;
                for (Direction from : kDirections)
                    if ((sources & bit(from)) && section->visible(opposite(from), d)) {
                        sees = true;
                        break;
                    }
                if (!sees) continue;
            }
            if (far && !lineOfSight(ndx, ndz, nsi, d)) continue;
            uint16_t& next = nodes_[nodeIndex(ndx, ndz, nsi)];
            if (next & kVisited) {
                next |= bit(d);
                continue;
            }
            next = uint16_t(kVisited | bit(d) | (dirs | bit(d)) << 6);
            // Columns without meshes yet are reached but not searched through (vanilla: not
            // compiled, or missing neighbors).
            if (columns_[ncol]) queue_.push_back(nodeIndex(ndx, ndz, nsi));
        }
    }
}

} // namespace mc
