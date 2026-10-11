// Math.h -- directions, block/chunk positions and axis-aligned boxes.
// Conventions follow vanilla: x east, z south, y up; Direction order D,U,N,S,W,E.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

#include <glm/glm.hpp>

namespace mc {

enum class Axis : uint8_t { X, Y, Z };

enum class Direction : uint8_t { Down, Up, North, South, West, East };

inline constexpr std::array<Direction, 6> kDirections = {Direction::Down,  Direction::Up,   Direction::North,
                                                         Direction::South, Direction::West, Direction::East};
// Horizontal directions in clockwise order seen from above (N, E, S, W).
inline constexpr std::array<Direction, 4> kHorizontal = {Direction::North, Direction::East, Direction::South,
                                                         Direction::West};

constexpr int index(Direction d) { return int(d); }
constexpr Direction opposite(Direction d) { return Direction(uint8_t(d) ^ 1u); }

constexpr Axis axisOf(Direction d) {
    switch (d) {
    case Direction::Down:
    case Direction::Up: return Axis::Y;
    case Direction::North:
    case Direction::South: return Axis::Z;
    default: return Axis::X;
    }
}

constexpr bool isPositive(Direction d) {
    return d == Direction::Up || d == Direction::South || d == Direction::East;
}

constexpr glm::ivec3 normal(Direction d) {
    switch (d) {
    case Direction::Down: return {0, -1, 0};
    case Direction::Up: return {0, 1, 0};
    case Direction::North: return {0, 0, -1};
    case Direction::South: return {0, 0, 1};
    case Direction::West: return {-1, 0, 0};
    case Direction::East: return {1, 0, 0};
    }
    return {0, 0, 0};
}

// Clockwise rotation seen from above; vertical directions are unchanged.
constexpr Direction rotateYClockwise(Direction d) {
    switch (d) {
    case Direction::North: return Direction::East;
    case Direction::East: return Direction::South;
    case Direction::South: return Direction::West;
    case Direction::West: return Direction::North;
    default: return d;
    }
}

// Index in kHorizontal (N=0, E=1, S=2, W=3), or -1 for vertical directions.
constexpr int horizontalIndex(Direction d) {
    switch (d) {
    case Direction::North: return 0;
    case Direction::East: return 1;
    case Direction::South: return 2;
    case Direction::West: return 3;
    default: return -1;
    }
}

std::string_view directionName(Direction d);
std::optional<Direction> directionFromName(std::string_view name);
// The direction whose normal is closest to v.
Direction nearestDirection(const glm::vec3& v);

struct BlockPos {
    int x = 0, y = 0, z = 0;

    constexpr BlockPos() = default;
    constexpr BlockPos(int x_, int y_, int z_) : x(x_), y(y_), z(z_) {}
    explicit constexpr BlockPos(const glm::ivec3& v) : x(v.x), y(v.y), z(v.z) {}

    static BlockPos containing(const glm::dvec3& p) {
        return {int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z))};
    }

    constexpr BlockPos offset(Direction d, int n = 1) const {
        const glm::ivec3 o = normal(d) * n;
        return {x + o.x, y + o.y, z + o.z};
    }
    constexpr BlockPos offset(int dx, int dy, int dz) const { return {x + dx, y + dy, z + dz}; }
    constexpr BlockPos above(int n = 1) const { return {x, y + n, z}; }
    constexpr BlockPos below(int n = 1) const { return {x, y - n, z}; }
    constexpr glm::ivec3 vec() const { return {x, y, z}; }
    glm::dvec3 center() const { return {x + 0.5, y + 0.5, z + 0.5}; }

    constexpr bool operator==(const BlockPos&) const = default;
    constexpr BlockPos operator+(const BlockPos& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr BlockPos operator-(const BlockPos& o) const { return {x - o.x, y - o.y, z - o.z}; }
};

struct ChunkPos {
    int x = 0, z = 0;

    constexpr ChunkPos() = default;
    constexpr ChunkPos(int x_, int z_) : x(x_), z(z_) {}
    static constexpr ChunkPos of(const BlockPos& p) { return {p.x >> 4, p.z >> 4}; }

    constexpr int minBlockX() const { return x << 4; }
    constexpr int minBlockZ() const { return z << 4; }
    constexpr bool operator==(const ChunkPos&) const = default;
    constexpr int64_t key() const { return (int64_t(x) << 32) ^ int64_t(uint32_t(z)); }
    constexpr int chebyshev(const ChunkPos& o) const {
        const int dx = x > o.x ? x - o.x : o.x - x;
        const int dz = z > o.z ? z - o.z : o.z - z;
        return dx > dz ? dx : dz;
    }
};

struct AABB {
    glm::dvec3 min{0.0};
    glm::dvec3 max{0.0};

    constexpr AABB() = default;
    constexpr AABB(const glm::dvec3& lo, const glm::dvec3& hi) : min(lo), max(hi) {}
    constexpr AABB(double x0, double y0, double z0, double x1, double y1, double z1)
        : min(x0, y0, z0), max(x1, y1, z1) {}

    AABB moved(const glm::dvec3& d) const { return {min + d, max + d}; }
    AABB inflated(double dx, double dy, double dz) const {
        return {min - glm::dvec3(dx, dy, dz), max + glm::dvec3(dx, dy, dz)};
    }
    // Box swept by moving this box by d.
    AABB expandedTowards(const glm::dvec3& d) const {
        AABB r = *this;
        for (int i = 0; i < 3; ++i) (d[i] < 0 ? r.min[i] : r.max[i]) += d[i];
        return r;
    }
    bool intersects(const AABB& o) const {
        return min.x < o.max.x && max.x > o.min.x && min.y < o.max.y && max.y > o.min.y && min.z < o.max.z &&
               max.z > o.min.z;
    }
    bool contains(const glm::dvec3& p) const {
        return p.x >= min.x && p.x < max.x && p.y >= min.y && p.y < max.y && p.z >= min.z && p.z < max.z;
    }
    glm::dvec3 center() const { return (min + max) * 0.5; }
    glm::dvec3 size() const { return max - min; }
};

// Floor division and modulo for negative coordinates.
constexpr int floorDiv(int a, int b) { return (a >= 0 ? a : a - b + 1) / b; }
constexpr int floorMod(int a, int b) {
    const int m = a % b;
    return m < 0 ? m + b : m;
}

} // namespace mc

template <>
struct std::hash<mc::BlockPos> {
    size_t operator()(const mc::BlockPos& p) const noexcept {
        uint64_t h = uint64_t(uint32_t(p.x)) * 0x9E3779B97F4A7C15ull;
        h ^= uint64_t(uint32_t(p.y)) * 0xC2B2AE3D27D4EB4Full + (h << 6) + (h >> 2);
        h ^= uint64_t(uint32_t(p.z)) * 0x165667B19E3779F9ull + (h << 6) + (h >> 2);
        return size_t(h);
    }
};

template <>
struct std::hash<mc::ChunkPos> {
    size_t operator()(const mc::ChunkPos& p) const noexcept {
        const uint64_t k = uint64_t(p.key());
        return size_t((k ^ (k >> 29)) * 0xBF58476D1CE4E5B9ull);
    }
};
