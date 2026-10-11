// BlockShapes.cpp -- per-state outline and collision shapes.
//
// The default shape is the union of the model's element boxes (after the
// blockstate rotation). Flat models (flowers, vines) outline their quads'
// bounds. Blocks whose vanilla shapes differ from their models (fences, walls,
// panes, gates, snow, cactus, ...) or that are drawn by code (chests, beds,
// signs, banners, heads) use the shapes below, in 1/16-block units.
#include <algorithm>
#include <cstring>
#include <map>

#include "resources/BlockModels.h"
#include "resources/ModelBaker.h"
#include "resources/ResourcePack.h"
#include "world/block/BlockRegistry.h"

namespace mc {
namespace {

AABB box(double x0, double y0, double z0, double x1, double y1, double z1) {
    return AABB(x0 / 16.0, y0 / 16.0, z0 / 16.0, x1 / 16.0, y1 / 16.0, z1 / 16.0);
}

// A box given for a north-facing block, turned to `facing` about the vertical axis.
AABB facing(const AABB& north, Direction f) {
    auto turn = [&](double x, double z) -> glm::dvec2 {
        switch (f) {
        case Direction::East: return {1.0 - z, x};
        case Direction::South: return {1.0 - x, 1.0 - z};
        case Direction::West: return {z, 1.0 - x};
        default: return {x, z};
        }
    };
    const glm::dvec2 a = turn(north.min.x, north.min.z), b = turn(north.max.x, north.max.z);
    return AABB(std::min(a.x, b.x), north.min.y, std::min(a.y, b.y), std::max(a.x, b.x), north.max.y, std::max(a.y, b.y));
}

Direction facingOf(const BlockRegistry& reg, StateId s) {
    return directionFromName(reg.value(s, "facing")).value_or(Direction::North);
}

// Vanilla CrossCollisionBlock.makeShapes for fences and panes: a post plus one arm per connected side.
Shape crossShape(const BlockRegistry& reg, StateId s, double node, double ext, double nodeHeight, double extBottom,
                 double extHeight) {
    const double f = 8 - node, g = 8 + node, h = 8 - ext, i = 8 + ext;
    Shape out{box(f, 0, f, g, nodeHeight, g)};
    if (reg.boolValue(s, "north")) out.push_back(box(h, extBottom, 0, i, extHeight, i));
    if (reg.boolValue(s, "south")) out.push_back(box(h, extBottom, h, i, extHeight, 16));
    if (reg.boolValue(s, "west")) out.push_back(box(0, extBottom, h, i, extHeight, i));
    if (reg.boolValue(s, "east")) out.push_back(box(h, extBottom, h, 16, extHeight, i));
    return out;
}

// Vanilla WallBlock.makeShapes: post when "up", low or tall arms per side.
Shape wallShape(const BlockRegistry& reg, StateId s, double postHeight, double lowHeight, double tallHeight) {
    Shape out;
    if (reg.boolValue(s, "up")) out.push_back(box(4, 0, 4, 12, postHeight, 12));
    auto arm = [&](const char* side, AABB lowBox, AABB tallBox) {
        const std::string_view v = reg.value(s, side);
        if (v == "low") out.push_back(lowBox);
        else if (v == "tall") out.push_back(tallBox);
    };
    arm("north", box(5, 0, 0, 11, lowHeight, 11), box(5, 0, 0, 11, tallHeight, 11));
    arm("south", box(5, 0, 5, 11, lowHeight, 16), box(5, 0, 5, 11, tallHeight, 16));
    arm("west", box(0, 0, 5, 11, lowHeight, 11), box(0, 0, 5, 11, tallHeight, 11));
    arm("east", box(5, 0, 5, 16, lowHeight, 11), box(5, 0, 5, 16, tallHeight, 11));
    return out;
}

struct Override {
    std::optional<Shape> outline, collision;
};

// Vanilla shapes that models do not give. Empty optional = keep the default.
Override shapeOverride(const BlockRegistry& reg, const Block& b, StateId s) {
    const std::string_view cls = b.className, name = shortId(b.id);
    Override o;
    if (cls == "fence") {
        o.outline = crossShape(reg, s, 2, 2, 16, 0, 16);
        o.collision = crossShape(reg, s, 2, 2, 24, 0, 24);
    } else if (cls == "pane") {
        o.outline = o.collision = crossShape(reg, s, 1, 1, 16, 16, 16);
    } else if (cls == "wall") {
        o.outline = wallShape(reg, s, 16, 14, 16);
        o.collision = wallShape(reg, s, 24, 24, 24);
    } else if (cls == "fence_gate") {
        const Direction f = facingOf(reg, s);
        const bool zAxis = f == Direction::North || f == Direction::South;
        const double top = reg.boolValue(s, "in_wall") ? 13 : 16;
        o.outline = Shape{zAxis ? box(0, 0, 6, 16, top, 10) : box(6, 0, 0, 10, top, 16)};
        o.collision = reg.boolValue(s, "open") ? Shape{} : Shape{zAxis ? box(0, 0, 6, 16, 24, 10) : box(6, 0, 0, 10, 24, 16)};
    } else if (cls == "snow_layer") {
        const int layers = reg.intValue(s, "layers", 1);
        o.outline = Shape{box(0, 0, 0, 16, layers * 2, 16)};
        o.collision = layers > 1 ? Shape{box(0, 0, 0, 16, (layers - 1) * 2, 16)} : Shape{};
    } else if (name == "cactus") {
        o.outline = Shape{box(1, 0, 1, 15, 16, 15)};
        o.collision = Shape{box(1, 0, 1, 15, 15, 15)};
    } else if (name == "soul_sand" || name == "mud") {
        o.collision = Shape{box(0, 0, 0, 16, 14, 16)};
    } else if (name == "honey_block") {
        o.collision = Shape{box(1, 0, 1, 15, 15, 15)};
    } else if (name == "lily_pad") {
        o.outline = o.collision = Shape{box(1, 0, 1, 15, 1.5, 15)};
    } else if (cls == "bed") {
        o.outline = o.collision = Shape{box(0, 0, 0, 16, 9, 16)};
    } else if (cls == "chest" || cls == "ender_chest") {
        AABB shape = box(1, 0, 1, 15, 14, 15);
        const std::string_view type = reg.value(s, "type");
        if (type == "left" || type == "right") {
            // The double chest reaches the edge toward its other half.
            const Direction f = facingOf(reg, s);
            const Direction toOther = type == "left" ? rotateYClockwise(f) : opposite(rotateYClockwise(f));
            if (toOther == Direction::North) shape.min.z = 0;
            if (toOther == Direction::South) shape.max.z = 1;
            if (toOther == Direction::West) shape.min.x = 0;
            if (toOther == Direction::East) shape.max.x = 1;
        }
        o.outline = o.collision = Shape{shape};
    } else if (cls == "sign" || cls == "banner") {
        o.outline = Shape{box(4, 0, 4, 12, 16, 12)};
        o.collision = Shape{};
    } else if (cls == "wall_sign") {
        o.outline = Shape{facing(box(0, 4.5, 14, 16, 12.5, 16), facingOf(reg, s))};
        o.collision = Shape{};
    } else if (cls == "wall_banner") {
        o.outline = Shape{facing(box(0, 0, 14, 16, 12.5, 16), facingOf(reg, s))};
        o.collision = Shape{};
    } else if (cls == "hanging_sign" || cls == "wall_hanging_sign") {
        o.outline = Shape{box(1, 0, 1, 15, 16, 15)};
        o.collision = Shape{};
    } else if (cls == "skull") {
        o.outline = o.collision = Shape{box(4, 0, 4, 12, 8, 12)};
    } else if (cls == "wall_skull") {
        o.outline = o.collision = Shape{facing(box(4, 4, 8, 12, 12, 16), facingOf(reg, s))};
    } else if (name.ends_with("shulker_box") || name == "barrier") {
        o.outline = o.collision = Shape{box(0, 0, 0, 16, 16, 16)};
    } else if (name == "decorated_pot") {
        o.outline = o.collision = Shape{box(1, 0, 1, 15, 16, 15)};
    } else if (name == "conduit") {
        o.outline = o.collision = Shape{box(5, 5, 5, 11, 11, 11)};
    } else if (cls == "torch" || cls == "redstone_torch") {
        o.outline = Shape{box(6, 0, 6, 10, 10, 10)};
    } else if (cls == "wall_torch" || cls == "redstone_wall_torch") {
        o.outline = Shape{facing(box(5.5, 3, 11, 10.5, 13, 16), facingOf(reg, s))};
    } else if (cls == "ladder") {
        o.outline = o.collision = Shape{facing(box(0, 0, 13, 16, 16, 16), facingOf(reg, s))};
    } else if (name == "scaffolding") {
        o.outline = Shape{box(0, 0, 0, 16, 16, 16)};
        o.collision = Shape{box(0, 14, 0, 16, 16, 16)};
    } else if (name == "light" || name == "structure_void") {
        o.outline = o.collision = Shape{};
    }
    return o;
}

// Shapes with identical boxes share one table entry.
std::string shapeKey(const Shape& s) {
    std::string key(s.size() * sizeof(AABB), '\0');
    if (!s.empty()) std::memcpy(key.data(), s.data(), key.size());
    return key;
}

} // namespace

void BlockRegistry::buildShapes() {
    shapes_.clear();
    std::map<std::string, uint16_t> index;
    auto intern = [&](Shape s) -> uint16_t {
        // Drop boxes inside another one (overlay elements); of identical boxes keep the first.
        Shape kept;
        for (size_t i = 0; i < s.size(); ++i) {
            bool contained = false;
            for (size_t j = 0; j < s.size() && !contained; ++j) {
                if (i == j) continue;
                const bool inside = glm::all(glm::greaterThanEqual(s[i].min, s[j].min)) &&
                                    glm::all(glm::lessThanEqual(s[i].max, s[j].max));
                const bool same = s[i].min == s[j].min && s[i].max == s[j].max;
                contained = inside && (!same || j < i);
            }
            if (!contained) kept.push_back(s[i]);
        }
        auto [it, inserted] = index.emplace(shapeKey(kept), uint16_t(shapes_.size()));
        if (inserted) shapes_.push_back(std::move(kept));
        return it->second;
    };
    intern({});                     // 0: empty
    intern({AABB(0, 0, 0, 1, 1, 1)}); // 1: full cube

    for (size_t s = 0; s < info_.size(); ++s) {
        StateInfo& si = info_[s];
        const Block& b = blocks_[si.block];
        Shape boxes;
        AABB bounds;
        bool haveBounds = false;
        for (const BakedChoice* c : modelChoices(StateId(s))) {
            const BakedModel* m = c->models.empty() ? nullptr : c->models[0];
            if (!m) continue;
            boxes.insert(boxes.end(), m->boxes.begin(), m->boxes.end());
            if (!m->quads.empty()) {
                bounds = haveBounds ? AABB(glm::min(bounds.min, m->quadBounds.min), glm::max(bounds.max, m->quadBounds.max))
                                    : m->quadBounds;
                haveBounds = true;
            }
        }
        Shape outline = boxes;
        if (outline.empty() && haveBounds) {
            // Flat models: their quads' bounds, at least a pixel thick.
            for (int a = 0; a < 3; ++a)
                if (bounds.max[a] - bounds.min[a] < 1.0 / 16.0) {
                    const double c = std::clamp((bounds.min[a] + bounds.max[a]) * 0.5, 1.0 / 32.0, 1.0 - 1.0 / 32.0);
                    bounds.min[a] = c - 1.0 / 32.0;
                    bounds.max[a] = c + 1.0 / 32.0;
                }
            bounds.min = glm::clamp(bounds.min, 0.0, 1.0);
            bounds.max = glm::clamp(bounds.max, 0.0, 1.0);
            outline.push_back(bounds);
        }
        Shape collision = boxes;
        if (si.flags & kAir) outline.clear(), collision.clear();
        Override o = shapeOverride(*this, b, StateId(s));
        if (o.outline) outline = *o.outline;
        if (o.collision) collision = *o.collision;
        // No-collision blocks (plants, fluids, signs...) per data/blocks/properties.json.
        if (!(si.flags & kHasCollision)) collision.clear();
        si.outline = intern(std::move(outline));
        si.collision = intern(std::move(collision));
    }
}

} // namespace mc
