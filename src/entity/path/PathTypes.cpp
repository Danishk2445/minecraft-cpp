#include "entity/path/PathTypes.h"

#include <mutex>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "resources/ResourcePack.h"
#include "world/block/BlockGetter.h"
#include "world/chunk/Chunk.h"

namespace mc {
namespace {

// The blocks whose isPathfindable(LAND) is false although their collision doesn't fill the
// block (vanilla's overrides): slabs, stairs, chests, beds, fences, panes and the like.
bool notPathfindableClass(std::string_view cls) {
    static const std::unordered_set<std::string_view> kClasses = {
        "anvil",         "bed",          "bell",        "cactus",       "cake",          "campfire",
        "candle_cake",   "layered_cauldron", "chain",   "chest",        "ender_chest",   "chorus_plant",
        "chorus_flower", "cocoa",        "composter",   "decorated_pot", "farmland",     "fence",
        "wall",          "pane",         "hopper",      "lantern",      "lightning_rod", "end_rod",
        "pointed_dripstone", "skull",    "wall_skull",  "slab",         "stairs",        "bamboo",
        "sea_pickle",    "big_dripleaf", "piston_head", "candle",       "turtle_egg",
    };
    return kClasses.contains(cls);
}

bool notPathfindableId(std::string_view id) {
    static const std::unordered_set<std::string_view> kIds = {
        "enchanting_table", "lectern",  "grindstone", "stonecutter", "brewing_stand", "daylight_detector",
        "dirt_path",        "conduit",  "azalea",     "flowering_azalea", "sniffer_egg", "cauldron",
        "lava_cauldron",    "end_portal_frame", "dragon_egg", "heavy_core",
    };
    return kIds.contains(id);
}

struct Cache {
    const BlockRegistry* registry = nullptr;
    std::vector<PathBlockInfo> states;
};

PathBlockInfo classify(const BlockRegistry& reg, StateId s) {
    const StateInfo& info = reg.info(s);
    const Block& block = reg.blockOf(s);
    const std::string_view id = shortId(block.id);
    const std::string_view cls = block.className;
    auto tagged = [&](const char* name) {
        const auto t = reg.tag(name);
        return t && reg.hasTag(s, *t);
    };
    const Shape& collision = reg.collisionShape(s);
    PathBlockInfo out;
    // isSolid: legacy solidity from the collision bounds (average size >= 0.729 or full height).
    if (!collision.empty()) {
        AABB bounds = collision[0];
        for (const AABB& b : collision) {
            bounds.min = glm::min(bounds.min, b.min);
            bounds.max = glm::max(bounds.max, b.max);
        }
        const glm::dvec3 size = bounds.max - bounds.min;
        out.solid = (size.x + size.y + size.z) / 3.0 >= 0.7291666666666666 || size.y >= 1.0;
    }
    const bool full = collision.size() == 1 && collision[0].min == glm::dvec3(0.0) && collision[0].max == glm::dvec3(1.0);
    // isPathfindable(LAND): no full collision, unless the block says otherwise.
    out.pathfindable = !full;
    if (cls == "door" || cls == "fence_gate" || cls == "trapdoor") out.pathfindable = reg.boolValue(s, "open");
    else if (cls == "snow_layer") out.pathfindable = reg.intValue(s, "layers", 1) < 5;
    else if (cls == "liquid") out.pathfindable = info.fluid.kind != FluidKind::Lava;
    else if (!collision.empty() && (notPathfindableClass(cls) || notPathfindableId(id))) out.pathfindable = false;
    out.burning = tagged("fire") || info.fluid.kind == FluidKind::Lava || id == "magma_block" || id == "lava_cauldron" ||
                  (tagged("campfires") && reg.boolValue(s, "lit"));

    if (info.flags & kAir) out.type = PathType::Open;
    else if (tagged("trapdoors") || id == "lily_pad" || id == "big_dripleaf") out.type = PathType::Trapdoor;
    else if (id == "powder_snow") out.type = PathType::PowderSnow;
    else if (id == "cactus" || id == "sweet_berry_bush") out.type = PathType::DamageOther;
    else if (id == "honey_block") out.type = PathType::StickyHoney;
    else if (id == "cocoa") out.type = PathType::Cocoa;
    else if (id == "wither_rose" || id == "pointed_dripstone") out.type = PathType::DamageCautious;
    else if (info.fluid.kind == FluidKind::Lava) out.type = PathType::Lava;
    else if (out.burning) out.type = PathType::DamageFire;
    else if (cls == "door") {
        out.type = reg.boolValue(s, "open") ? PathType::DoorOpen
                   : tagged("wooden_doors")  ? PathType::DoorWoodClosed
                                             : PathType::DoorIronClosed;
    } else if (tagged("rails")) out.type = PathType::Rail;
    else if (tagged("leaves")) out.type = PathType::Leaves;
    else if (tagged("fences") || tagged("walls") || (cls == "fence_gate" && !reg.boolValue(s, "open")))
        out.type = PathType::Fence;
    else if (!out.pathfindable) out.type = PathType::Blocked;
    else out.type = info.fluid.kind == FluidKind::Water ? PathType::Water : PathType::Open;
    return out;
}

} // namespace

float defaultMalus(PathType type) {
    switch (type) {
    case PathType::Blocked: return -1.0f;
    case PathType::PowderSnow: return -1.0f;
    case PathType::Fence: return -1.0f;
    case PathType::Lava: return -1.0f;
    case PathType::Water: return 8.0f;
    case PathType::WaterBorder: return 8.0f;
    case PathType::UnpassableRail: return -1.0f;
    case PathType::DangerFire: return 8.0f;
    case PathType::DamageFire: return 16.0f;
    case PathType::DangerOther: return 8.0f;
    case PathType::DamageOther: return -1.0f;
    case PathType::DoorWoodClosed: return -1.0f;
    case PathType::DoorIronClosed: return -1.0f;
    case PathType::Breach: return 4.0f;
    case PathType::Leaves: return -1.0f;
    case PathType::StickyHoney: return 8.0f;
    default: return 0.0f;
    }
}

PathMalus defaultMaluses() {
    PathMalus m{};
    for (size_t i = 0; i < kPathTypeCount; ++i) m[i] = defaultMalus(PathType(i));
    return m;
}

const PathBlockInfo& pathBlockInfo(const BlockRegistry& registry, StateId state) {
    // Built once per registry (thread-safe: path finding may one day run on workers).
    static std::mutex mutex;
    static std::vector<std::unique_ptr<Cache>> caches;
    thread_local const Cache* last = nullptr;
    if (!last || last->registry != &registry) {
        std::lock_guard lock(mutex);
        last = nullptr;
        for (const auto& c : caches)
            if (c->registry == &registry) last = c.get();
        if (!last) {
            auto c = std::make_unique<Cache>();
            c->registry = &registry;
            c->states.resize(registry.stateCount());
            for (size_t s = 0; s < registry.stateCount(); ++s) c->states[s] = classify(registry, StateId(s));
            last = c.get();
            caches.push_back(std::move(c));
        }
    }
    return last->states[state];
}

PathType pathTypeAt(const BlockGetter& level, const BlockPos& p) {
    const BlockRegistry& reg = level.registry();
    auto stateType = [&](int x, int y, int z) { return pathBlockInfo(reg, level.getBlock({x, y, z})).type; };
    const PathType here = stateType(p.x, p.y, p.z);
    if (here != PathType::Open || p.y < kWorldMinY + 1) return here;
    switch (stateType(p.x, p.y - 1, p.z)) {
    case PathType::Open:
    case PathType::Water:
    case PathType::Lava:
    case PathType::Walkable: return PathType::Open;
    case PathType::DamageFire: return PathType::DamageFire;
    case PathType::DamageOther: return PathType::DamageOther;
    case PathType::StickyHoney: return PathType::StickyHoney;
    case PathType::PowderSnow: return PathType::DangerPowderSnow;
    case PathType::DamageCautious: return PathType::DamageCautious;
    case PathType::Trapdoor: return PathType::DangerTrapdoor;
    default: break;
    }
    // A floor: walkable, unless something dangerous is next to it (vanilla checkNeighbourBlocks).
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dz = -1; dz <= 1; ++dz) {
                if (dx == 0 && dz == 0) continue;
                const PathType t = stateType(p.x + dx, p.y + dy, p.z + dz);
                if (t == PathType::DamageOther) return PathType::DangerOther;
                if (t == PathType::DamageFire || t == PathType::Lava) return PathType::DangerFire;
                if (t == PathType::Water) return PathType::WaterBorder;
                if (t == PathType::DamageCautious) return PathType::DamageCautious;
            }
    return PathType::Walkable;
}

double floorLevel(const BlockGetter& level, const BlockPos& p) {
    const BlockPos below = p.offset(0, -1, 0);
    const Shape& shape = level.registry().collisionShape(level.getBlock(below));
    double top = 0.0;
    for (const AABB& b : shape) top = std::max(top, b.max.y);
    return double(below.y) + (shape.empty() ? 0.0 : top);
}

} // namespace mc
