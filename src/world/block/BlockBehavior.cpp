#include "world/block/BlockBehavior.h"

#include "resources/ResourcePack.h"
#include "world/block/BlockRules.h"

namespace mc {
namespace {

struct Tags {
    TagId fences, woodenFences, walls, leaves, shulkers;
    explicit Tags(const BlockRegistry& reg) {
        auto tag = [&](const char* n) { return reg.tag(n).value_or(TagId(0)); };
        fences = tag("fences");
        woodenFences = tag("wooden_fences");
        walls = tag("walls");
        leaves = tag("leaves");
        shulkers = tag("shulker_boxes");
    }
};

const Tags& tags(const BlockRegistry& reg) {
    static thread_local const BlockRegistry* owner = nullptr;
    static thread_local std::optional<Tags> t;
    if (owner != &reg) {
        t.emplace(reg);
        owner = &reg;
    }
    return *t;
}

Direction facingOf(const BlockRegistry& reg, StateId s) {
    return directionFromName(reg.value(s, "facing")).value_or(Direction::North);
}

StateId set(const BlockRegistry& reg, StateId s, std::string_view prop, std::string_view value) {
    return reg.with(s, prop, value).value_or(s);
}

// Vanilla Block.isExceptionForConnection: solid faces that fences and panes still skip.
bool connectionException(const BlockRegistry& reg, StateId s) {
    const Tags& t = tags(reg);
    const std::string_view id = shortId(reg.blockOf(s).id);
    return reg.hasTag(s, t.leaves) || reg.hasTag(s, t.shulkers) || id == "barrier" || id == "carved_pumpkin" ||
           id == "jack_o_lantern" || id == "melon" || id == "pumpkin";
}

// Vanilla FenceGateBlock.connectsToDirection: gates join walls and fences along their own axis.
bool gateConnects(const BlockRegistry& reg, StateId gate, Direction dir) {
    if (reg.blockOf(gate).className != "fence_gate") return false;
    return axisOf(facingOf(reg, gate)) != axisOf(dir);
}

bool connects(const BlockRegistry& reg, const BlockGetter& level, StateId self, const BlockPos& pos, Direction dir) {
    const BlockPos np = pos.offset(dir);
    const StateId n = level.getBlock(np);
    const std::string_view cls = reg.blockOf(self).className;
    const Tags& t = tags(reg);
    const bool solidSide = level.sturdy(np, opposite(dir)) && !connectionException(reg, n);
    const std::string_view ncls = reg.blockOf(n).className;
    if (cls == "fence") {
        const bool sameFence = reg.hasTag(n, t.fences) && reg.hasTag(n, t.woodenFences) == reg.hasTag(self, t.woodenFences);
        return solidSide || sameFence || gateConnects(reg, n, dir);
    }
    if (cls == "pane") return solidSide || ncls == "pane" || reg.hasTag(n, t.walls);
    if (cls == "wall") return reg.hasTag(n, t.walls) || solidSide || ncls == "pane" || gateConnects(reg, n, dir);
    return false;
}

bool isStairs(const BlockRegistry& reg, StateId s) { return reg.blockOf(s).className == "stairs"; }

} // namespace

StateId withConnections(const BlockRegistry& reg, const BlockGetter& level, StateId self, const BlockPos& pos) {
    const std::string_view cls = reg.blockOf(self).className;
    if (cls != "fence" && cls != "pane" && cls != "wall") return self;
    bool side[4];
    for (int i = 0; i < 4; ++i) side[i] = connects(reg, level, self, pos, kHorizontal[size_t(i)]);
    StateId s = self;
    if (cls != "wall") {
        for (int i = 0; i < 4; ++i)
            s = set(reg, s, directionName(kHorizontal[size_t(i)]), side[i] ? "true" : "false");
        return s;
    }
    // Walls: a side is tall when the block above covers it, the post stays unless the wall runs straight.
    const BlockPos above = pos.above();
    const bool coveredAbove = level.sturdy(above, Direction::Down);
    const bool aboveWall = reg.hasTag(level.getBlock(above), tags(reg).walls);
    for (int i = 0; i < 4; ++i)
        s = set(reg, s, directionName(kHorizontal[size_t(i)]), !side[i] ? "none" : (coveredAbove || aboveWall) ? "tall" : "low");
    // kHorizontal is N, E, S, W.
    const bool straight = (side[0] && side[2] && !side[1] && !side[3]) || (side[1] && side[3] && !side[0] && !side[2]);
    const bool post = !straight || (aboveWall && reg.boolValue(level.getBlock(above), "up")) || coveredAbove;
    return set(reg, s, "up", post ? "true" : "false");
}

StateId withStairsShape(const BlockRegistry& reg, const BlockGetter& level, StateId self, const BlockPos& pos) {
    if (!isStairs(reg, self)) return self;
    const Direction facing = facingOf(reg, self);
    const std::string_view half = reg.value(self, "half");
    auto canTakeShape = [&](Direction face) {
        const StateId n = level.getBlock(pos.offset(face));
        return !isStairs(reg, n) || facingOf(reg, n) != facing || reg.value(n, "half") != half;
    };
    const StateId front = level.getBlock(pos.offset(facing));
    if (isStairs(reg, front) && reg.value(front, "half") == half) {
        const Direction f2 = facingOf(reg, front);
        if (axisOf(f2) != axisOf(facing) && canTakeShape(opposite(f2)))
            return set(reg, self, "shape", f2 == opposite(rotateYClockwise(facing)) ? "outer_left" : "outer_right");
    }
    const StateId back = level.getBlock(pos.offset(opposite(facing)));
    if (isStairs(reg, back) && reg.value(back, "half") == half) {
        const Direction f3 = facingOf(reg, back);
        if (axisOf(f3) != axisOf(facing) && canTakeShape(f3))
            return set(reg, self, "shape", f3 == opposite(rotateYClockwise(facing)) ? "inner_left" : "inner_right");
    }
    return set(reg, self, "shape", "straight");
}

bool canBurn(const BlockRegistry& reg, StateId s) { return reg.blockOf(s).igniteOdds > 0 && !reg.boolValue(s, "waterlogged"); }

namespace {
bool soulFireBase(const BlockRegistry& reg, StateId s) {
    const std::string_view id = shortId(reg.blockOf(s).id);
    return id == "soul_sand" || id == "soul_soil";
}
} // namespace

StateId fireStateAt(const BlockRegistry& reg, const BlockGetter& level, const BlockPos& pos) {
    const StateId below = level.getBlock(pos.below());
    if (soulFireBase(reg, below)) return reg.defaultState("soul_fire");
    const StateId fire = reg.defaultState("fire");
    if (canBurn(reg, below) || level.sturdy(pos.below(), Direction::Up)) return fire;
    StateId s = fire;
    for (Direction d : kDirections)
        if (d != Direction::Down) s = set(reg, s, directionName(d), canBurn(reg, level.getBlock(pos.offset(d))) ? "true" : "false");
    return s;
}

bool fireCanSurvive(const BlockRegistry& reg, const BlockGetter& level, StateId fire, const BlockPos& pos) {
    if (shortId(reg.blockOf(fire).id) == "soul_fire") return soulFireBase(reg, level.getBlock(pos.below()));
    if (level.sturdy(pos.below(), Direction::Up)) return true;
    for (Direction d : kDirections)
        if (canBurn(reg, level.getBlock(pos.offset(d)))) return true;
    return false;
}

StateId updateShape(const BlockRegistry& reg, const BlockGetter& level, StateId self, const BlockPos& pos, Direction dir,
                    StateId neighbor) {
    const Block& block = reg.blockOf(self);
    const std::string_view cls = block.className;
    const std::string_view half = reg.value(self, "half");

    // Two-block blocks: the other half must stay (doors copy its open/hinge state).
    if ((cls == "door" || cls == "double_plant" || cls == "tall_water_plant" || cls == "small_dripleaf") &&
        axisOf(dir) == Axis::Y && (half == "lower") == (dir == Direction::Up)) {
        const bool otherHalf = reg.info(neighbor).block == block.index && reg.value(neighbor, "half") != half;
        if (!otherHalf) return cls == "tall_water_plant" ? reg.defaultState("water") : BlockRegistry::kAirState;
        if (cls != "door") return self;
        StateId s = self;
        for (const char* prop : {"facing", "open", "hinge", "powered"}) s = set(reg, s, prop, reg.value(neighbor, prop));
        return s;
    }
    if (cls == "bed") {
        // Head and foot: the part `facing` points to (from the foot) must remain.
        const bool foot = reg.value(self, "part") == "foot";
        const Direction toOther = foot ? facingOf(reg, self) : opposite(facingOf(reg, self));
        if (dir == toOther && (reg.info(neighbor).block != block.index || reg.value(neighbor, "part") == reg.value(self, "part")))
            return BlockRegistry::kAirState;
        return self;
    }
    if (cls == "fire" || shortId(block.id) == "soul_fire") {
        // FireBlock.updateShape: out without support, else re-attached (keeping its age).
        if (!fireCanSurvive(reg, level, self, pos)) return BlockRegistry::kAirState;
        const StateId fresh = fireStateAt(reg, level, pos);
        return reg.info(fresh).block == block.index && cls == "fire" ? set(reg, fresh, "age", reg.value(self, "age")) : fresh;
    }
    // Losing support pops the block (plants, torches, ladders, signs, carpets, ...).
    if (!reg.rules().canSurvive(level, self, pos)) {
        const bool waterlogged =
            reg.boolValue(self, "waterlogged") || block.className == "water_plant" || block.className == "kelp";
        return waterlogged ? reg.defaultState("water") : BlockRegistry::kAirState;
    }
    if (cls == "fence" || cls == "pane" || cls == "wall") {
        if (axisOf(dir) == Axis::Y && cls != "wall") return self;
        return withConnections(reg, level, self, pos);
    }
    if (cls == "stairs" && axisOf(dir) != Axis::Y) return withStairsShape(reg, level, self, pos);
    if (cls == "attached_stem" && dir == facingOf(reg, self)) {
        // AttachedStemBlock: its fruit gone, a ripe stem again.
        const bool pumpkin = shortId(block.id) == "attached_pumpkin_stem";
        if (shortId(reg.blockOf(neighbor).id) != (pumpkin ? "pumpkin" : "melon"))
            return set(reg, reg.defaultState(pumpkin ? "pumpkin_stem" : "melon_stem"), "age", "7");
    }
    if (dir == Direction::Up && shortId(reg.blockOf(neighbor).id) == "bamboo") {
        // A bamboo shoot grows into a stalk; a stalk thickens under an older one.
        if (shortId(block.id) == "bamboo_sapling") return reg.defaultState("bamboo");
        if (cls == "bamboo" && reg.intValue(neighbor, "age") > reg.intValue(self, "age")) return set(reg, self, "age", "1");
    }
    if (cls == "snowy" && dir == Direction::Up) {
        const std::string_view n = shortId(reg.blockOf(neighbor).id);
        return set(reg, self, "snowy", n == "snow" || n == "snow_block" ? "true" : "false");
    }
    if (cls == "fence_gate" && axisOf(dir) != Axis::Y && axisOf(dir) != axisOf(facingOf(reg, self))) {
        // Lowered to sit flush when a wall is beside it.
        const bool wall = reg.hasTag(neighbor, tags(reg).walls) ||
                          reg.hasTag(level.getBlock(pos.offset(opposite(dir))), tags(reg).walls);
        return set(reg, self, "in_wall", wall ? "true" : "false");
    }
    if (cls == "chest" && axisOf(dir) != Axis::Y) {
        const std::string_view type = reg.value(self, "type");
        const Direction facing = facingOf(reg, self);
        auto connected = [&](StateId s) {
            const std::string_view t = reg.value(s, "type");
            const Direction f = facingOf(reg, s);
            return t == "left" ? rotateYClockwise(f) : opposite(rotateYClockwise(f));
        };
        if (reg.info(neighbor).block == block.index) {
            const std::string_view nt = reg.value(neighbor, "type");
            if (type == "single" && nt != "single" && facingOf(reg, neighbor) == facing && connected(neighbor) == opposite(dir))
                return set(reg, self, "type", nt == "left" ? "right" : "left");
        } else if (type != "single" && connected(self) == dir) {
            return set(reg, self, "type", "single");
        }
    }
    return self;
}

StateId scheduledTickResult(const BlockRegistry& reg, const BlockGetter&, StateId self, const BlockPos&) {
    if (reg.blockOf(self).className == "button" && reg.boolValue(self, "powered")) return set(reg, self, "powered", "false");
    return self;
}

std::optional<UseResult> useBlock(const BlockRegistry& reg, StateId self, Direction playerFacing) {
    const Block& b = reg.blockOf(self);
    const std::string_view cls = b.className, id = shortId(b.id);
    const bool open = reg.boolValue(self, "open");
    if ((cls == "door" && id != "iron_door") || (cls == "trapdoor" && id != "iron_trapdoor"))
        return UseResult{set(reg, self, "open", open ? "false" : "true")};
    if (cls == "fence_gate") {
        if (open) return UseResult{set(reg, self, "open", "false")};
        // Opening swings away from the player.
        StateId s = self;
        if (facingOf(reg, s) == opposite(playerFacing)) s = set(reg, s, "facing", directionName(playerFacing));
        return UseResult{set(reg, s, "open", "true")};
    }
    if (cls == "lever") return UseResult{set(reg, self, "powered", reg.boolValue(self, "powered") ? "false" : "true")};
    if (cls == "button") {
        if (reg.boolValue(self, "powered")) return UseResult{self};
        // Vanilla: wooden buttons stay pressed 30 ticks, stone ones 20.
        const bool stone = id == "stone_button" || id == "polished_blackstone_button";
        return UseResult{set(reg, self, "powered", "true"), stone ? 20 : 30};
    }
    return std::nullopt;
}

} // namespace mc
