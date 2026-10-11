#include "core/Math.h"

namespace mc {

std::string_view directionName(Direction d) {
    switch (d) {
    case Direction::Down: return "down";
    case Direction::Up: return "up";
    case Direction::North: return "north";
    case Direction::South: return "south";
    case Direction::West: return "west";
    case Direction::East: return "east";
    }
    return "?";
}

std::optional<Direction> directionFromName(std::string_view name) {
    for (Direction d : kDirections)
        if (directionName(d) == name) return d;
    // Blockstate files use "bottom"/"top" in a few legacy places.
    if (name == "bottom") return Direction::Down;
    if (name == "top") return Direction::Up;
    return std::nullopt;
}

Direction nearestDirection(const glm::vec3& v) {
    Direction best = Direction::North;
    float bestDot = -1e30f;
    for (Direction d : kDirections) {
        const float dot = glm::dot(v, glm::vec3(normal(d)));
        if (dot > bestDot) {
            bestDot = dot;
            best = d;
        }
    }
    return best;
}

} // namespace mc
