#include "game/Placement.h"

#include <cmath>
#include <string>

#include "entity/Player.h"
#include "resources/ResourcePack.h"
#include "world/block/BlockBehavior.h"
#include "world/block/BlockRules.h"
#include "world/chunk/Chunk.h"

namespace mc {
namespace {

StateId set(const BlockRegistry& reg, StateId s, std::string_view prop, std::string_view value) {
    return reg.with(s, prop, value).value_or(s);
}

bool hasValue(const BlockRegistry& reg, const Block& b, std::string_view prop, std::string_view value) {
    for (uint16_t id : b.props) {
        const Property& p = reg.property(id);
        if (p.name == prop) return p.index(value) >= 0;
    }
    return false;
}

bool hasProp(const BlockRegistry& reg, const Block& b, std::string_view prop) {
    for (uint16_t id : b.props)
        if (reg.property(id).name == prop) return true;
    return false;
}

// The wall-mounted variant of a floor block (torch -> wall_torch, oak_sign -> oak_wall_sign).
const Block* wallVariant(const BlockRegistry& reg, const Block& b) {
    std::string id(shortId(b.id));
    std::string wall;
    if (id == "torch") wall = "wall_torch";
    else if (id == "soul_torch") wall = "soul_wall_torch";
    else if (id == "redstone_torch") wall = "redstone_wall_torch";
    else if (id.ends_with("_hanging_sign")) wall = id.substr(0, id.size() - 13) + "_wall_hanging_sign";
    else if (id.ends_with("_sign")) wall = id.substr(0, id.size() - 5) + "_wall_sign";
    else if (id.ends_with("_banner")) wall = id.substr(0, id.size() - 7) + "_wall_banner";
    else if (id.ends_with("_head")) wall = id.substr(0, id.size() - 5) + "_wall_head";
    else if (id.ends_with("_skull")) wall = id.substr(0, id.size() - 6) + "_wall_skull";
    else if (id.ends_with("_coral_fan")) wall = id.substr(0, id.size() - 4) + "_wall_fan";
    else return nullptr;
    return reg.find(wall);
}

double fractional(double v) { return v - std::floor(v); }

} // namespace

Direction horizontalFacing(float yaw) {
    // Vanilla: floor(yaw / 90 + 0.5) & 3 indexes south, west, north, east.
    static constexpr Direction order[4] = {Direction::South, Direction::West, Direction::North, Direction::East};
    return order[size_t(int(std::floor(yaw / 90.0f + 0.5f)) & 3)];
}

Direction nearestLookingDirection(float yaw, float pitch) {
    const float y = yaw * 3.14159265f / 180.0f, p = pitch * 3.14159265f / 180.0f;
    const glm::vec3 look(-std::sin(y) * std::cos(p), -std::sin(p), std::cos(y) * std::cos(p));
    const glm::vec3 a = glm::abs(look);
    if (a.y >= a.x && a.y >= a.z) return look.y > 0 ? Direction::Up : Direction::Down;
    if (a.x >= a.z) return look.x > 0 ? Direction::East : Direction::West;
    return look.z > 0 ? Direction::South : Direction::North;
}

std::optional<StateId> mergeIntoClicked(const BlockRegistry& reg, StateId clicked, const Block& block,
                                        Direction clickedFace, double hitYInBlock) {
    if (reg.info(clicked).block != block.index || block.className != "slab") return std::nullopt;
    const std::string_view type = reg.value(clicked, "type");
    // Vanilla SlabBlock.canBeReplaced: a bottom slab clicked from above (or its upper half from the side).
    const bool upperHalf = hitYInBlock > 0.5;
    if (type == "bottom" && (clickedFace == Direction::Up || (axisOf(clickedFace) != Axis::Y && upperHalf)))
        return set(reg, clicked, "type", "double");
    if (type == "top" && (clickedFace == Direction::Down || (axisOf(clickedFace) != Axis::Y && !upperHalf)))
        return set(reg, clicked, "type", "double");
    return std::nullopt;
}

std::vector<std::pair<BlockPos, StateId>> placementStates(const BlockRegistry& reg, const BlockGetter& level,
                                                          const Block& placed, const PlaceContext& ctx) {
    const Block* block = &placed;
    const BlockPos pos = ctx.pos;
    const Direction face = ctx.clickedFace;
    const Direction horizontal = horizontalFacing(ctx.yaw);
    const double hitY = fractional(ctx.hitPoint.y);
    const bool sideClick = axisOf(face) != Axis::Y;

    // Floor items stuck to a wall become their wall variant.
    if (const Block* wall = wallVariant(reg, placed)) {
        if (sideClick) block = wall;
        else if (face == Direction::Down && block->className != "sign" && placed.className != "coral") return {};
    }
    StateId s = block->defaultState;
    const std::string_view cls = block->className;
    std::vector<std::pair<BlockPos, StateId>> out;

    if (cls == "stairs") {
        s = set(reg, s, "facing", directionName(horizontal));
        s = set(reg, s, "half", face != Direction::Down && (face == Direction::Up || hitY <= 0.5) ? "bottom" : "top");
    } else if (cls == "slab") {
        s = set(reg, s, "type", face != Direction::Down && (face == Direction::Up || hitY <= 0.5) ? "bottom" : "top");
    } else if (cls == "trapdoor") {
        if (!ctx.replacingClicked && sideClick) {
            s = set(reg, s, "facing", directionName(face));
            s = set(reg, s, "half", hitY > 0.5 ? "top" : "bottom");
        } else {
            s = set(reg, s, "facing", directionName(opposite(horizontal)));
            s = set(reg, s, "half", face == Direction::Up ? "bottom" : "top");
        }
    } else if (cls == "door" || cls == "fence_gate" || cls == "bed") {
        s = set(reg, s, "facing", directionName(horizontal));
    } else if (cls == "button" || cls == "lever" || shortId(block->id) == "grindstone") {
        const char* where = face == Direction::Up ? "floor" : face == Direction::Down ? "ceiling" : "wall";
        s = set(reg, set(reg, s, "face", where), "facing", directionName(sideClick ? face : horizontal));
    } else if (cls == "wall_torch" || cls == "redstone_wall_torch" || cls == "ladder" || cls == "wall_sign" ||
               cls == "wall_banner" || cls == "wall_skull" || cls == "coral_wall_fan" || cls == "wall_hanging_sign") {
        if (!sideClick) return {};
        s = set(reg, s, "facing", directionName(face));
    } else if (cls == "sign" || cls == "banner" || cls == "skull") {
        // 16 rotations. Signs and banners show their front at rotation 0 to the
        // south; heads look north at 0, so they take the player's yaw unturned.
        const float yaw = cls == "skull" ? ctx.yaw : ctx.yaw + 180.0f;
        const int r = int(std::floor(yaw * 16.0f / 360.0f + 0.5f)) & 15;
        s = set(reg, s, "rotation", std::to_string(r));
    } else if (cls == "rail" || cls == "powered_rail") {
        s = set(reg, s, "shape", axisOf(horizontal) == Axis::X ? "east_west" : "north_south");
    } else if (cls == "chest") {
        s = set(reg, s, "facing", directionName(opposite(horizontal)));
    } else if (cls == "hopper") {
        const Direction d = opposite(face);
        s = set(reg, s, "facing", directionName(axisOf(d) == Axis::Y ? Direction::Down : d));
    } else if (cls == "piston" || cls == "dispenser" || shortId(block->id) == "barrel") {
        s = set(reg, s, "facing", directionName(opposite(nearestLookingDirection(ctx.yaw, ctx.pitch))));
    } else if (cls == "observer") {
        s = set(reg, s, "facing", directionName(nearestLookingDirection(ctx.yaw, ctx.pitch)));
    } else if (shortId(block->id).find("anvil") != std::string_view::npos) {
        s = set(reg, s, "facing", directionName(rotateYClockwise(horizontal)));
    } else if (cls == "lantern") {
        s = set(reg, s, "hanging", face == Direction::Down ? "true" : "false");
    } else if (cls == "shulker_box") {
        s = set(reg, s, "facing", directionName(face));
    } else if (cls == "decorated_pot") {
        s = set(reg, s, "facing", directionName(horizontal));
    } else if (cls == "end_rod" || cls == "lightning_rod" || cls == "amethyst") {
        s = set(reg, s, "facing", directionName(face));
    } else if (hasProp(reg, *block, "facing")) {
        // Generic: four-way blocks face the player; six-way ones face away from where it looks.
        if (hasValue(reg, *block, "facing", "up"))
            s = set(reg, s, "facing", directionName(opposite(nearestLookingDirection(ctx.yaw, ctx.pitch))));
        else
            s = set(reg, s, "facing", directionName(opposite(horizontal)));
    }
    if (hasProp(reg, *block, "axis")) {
        static constexpr const char* axes[] = {"x", "y", "z"};
        s = set(reg, s, "axis", axes[int(axisOf(face))]);
    }
    if (hasProp(reg, *block, "waterlogged")) s = set(reg, s, "waterlogged", level.isWaterSource(pos) ? "true" : "false");
    s = withConnections(reg, level, s, pos);
    s = withStairsShape(reg, level, s, pos);
    if (cls == "fence_gate") {
        const Direction side = rotateYClockwise(horizontal);
        const TagId walls = reg.tag("walls").value_or(TagId(0));
        const bool wall = level.hasTag(pos.offset(side), walls) || level.hasTag(pos.offset(opposite(side)), walls);
        s = set(reg, s, "in_wall", wall ? "true" : "false");
    }
    if (cls == "chest" && !ctx.sneaking) {
        // Join a single chest beside it that faces the same way.
        const Direction facing = opposite(horizontal);
        auto partner = [&](Direction d) {
            const StateId n = level.getBlock(pos.offset(d));
            return reg.info(n).block == block->index && reg.value(n, "type") == "single" &&
                   reg.value(n, "facing") == directionName(facing);
        };
        if (partner(rotateYClockwise(facing))) s = set(reg, s, "type", "left");
        else if (partner(opposite(rotateYClockwise(facing)))) s = set(reg, s, "type", "right");
    }

    if (!reg.rules().canSurvive(level, s, pos)) return {};
    out.emplace_back(pos, s);

    // Second halves.
    auto freeAt = [&](const BlockPos& p) {
        return p.y >= kWorldMinY && p.y <= kWorldMaxY && (level.info(p).flags & (kAir | kReplaceable));
    };
    if (reg.value(s, "half") == "lower") {
        const BlockPos up = pos.above();
        if (!freeAt(up)) return {};
        StateId top = set(reg, s, "half", "upper");
        if (hasProp(reg, *block, "waterlogged")) top = set(reg, top, "waterlogged", level.isWaterSource(up) ? "true" : "false");
        out.emplace_back(up, top);
        if (cls == "door") {
            // Vanilla hinge: toward the side with more blocks, else by where the click landed.
            const Direction left = opposite(rotateYClockwise(horizontal)), right = rotateYClockwise(horizontal);
            auto solid = [&](Direction d) {
                return int(level.sturdy(pos.offset(d), opposite(d))) + int(level.sturdy(up.offset(d), opposite(d)));
            };
            const int l = solid(left), r = solid(right);
            bool hingeRight = r > l;
            if (l == r) {
                const glm::dvec3 local = ctx.hitPoint - glm::dvec3(pos.vec());
                const double along = axisOf(horizontal) == Axis::X ? local.z : local.x;
                const bool positiveSide = horizontal == Direction::North || horizontal == Direction::East;
                hingeRight = positiveSide ? along < 0.5 : along > 0.5;
            }
            for (auto& [p, st] : out) st = set(reg, st, "hinge", hingeRight ? "right" : "left");
        }
    } else if (cls == "bed") {
        const BlockPos head = pos.offset(horizontal);
        if (!freeAt(head) || !level.sturdy(head.below(), Direction::Up)) return {};
        out[0].second = set(reg, s, "part", "foot");
        out.emplace_back(head, set(reg, s, "part", "head"));
    }
    return out;
}

float destroyProgressPerTick(const Block& block, const Player& player) {
    if (block.hardness < 0.0f) return 0.0f;
    if (block.hardness == 0.0f) return 1.0f;
    float speed = 1.0f; // empty hand
    if (player.inWater && player.waterHeight > player.eyeHeight()) speed /= 5.0f;
    if (!player.onGround) speed /= 5.0f;
    return speed / block.hardness / (block.requiresTool ? 100.0f : 30.0f);
}

} // namespace mc
