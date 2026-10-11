// TreeDecorators.cpp -- vanilla tree decorators: trunk_vine, leave_vine, cocoa,
// beehive, alter_ground, and the 1.21.5 place_on_ground (leaf litter) and
// attached_to_logs (mushrooms on fallen logs).
#include <array>

#include "world/gen/feature/Feature.h"
#include "world/gen/feature/TreeConfig.h"

namespace mc {
namespace {

// Vine states hanging on the block at side d (`d` = true), indexed by Direction.
struct VineStates {
    std::array<StateId, 6> on{};
    uint16_t block = 0;
    explicit VineStates(const BlockRegistry& reg) {
        const StateId base = reg.defaultState("vine");
        block = reg.info(base).block;
        for (Direction d : kHorizontal) on[size_t(d)] = withProp(reg, base, directionName(d), true);
    }
};

// Vanilla Util.shuffle (Fisher-Yates from the back).
void shuffle(std::vector<BlockPos>& v, Xoroshiro& r) {
    for (int i = int(v.size()); i > 1; --i) std::swap(v[size_t(i - 1)], v[size_t(r.nextInt(i))]);
}

// Vines on every side of every log, each with chance 2/3.
class TrunkVineDecorator final : public TreeDecorator {
public:
    explicit TrunkVineDecorator(const BlockRegistry& reg) : vines_(reg) {}
    void place(DecoratorContext& c) const override {
        static constexpr Direction sides[] = {Direction::West, Direction::East, Direction::North, Direction::South};
        for (const BlockPos& log : c.logs)
            for (Direction d : sides) {
                const BlockPos p = log.offset(d);
                if (c.random.nextInt(3) > 0 && c.isAir(p)) c.setBlock(p, vines_.on[size_t(opposite(d))]);
            }
    }

private:
    VineStates vines_;
};

// Vines hanging up to 5 blocks from the sides of leaves.
class LeaveVineDecorator final : public TreeDecorator {
public:
    LeaveVineDecorator(const BlockRegistry& reg, float probability) : vines_(reg), probability_(probability) {}
    void place(DecoratorContext& c) const override {
        static constexpr Direction sides[] = {Direction::West, Direction::East, Direction::North, Direction::South};
        for (const BlockPos& leaf : c.leaves)
            for (Direction d : sides) {
                const BlockPos p = leaf.offset(d);
                if (c.random.nextFloat() < probability_ && c.isAir(p)) hang(c, p, vines_.on[size_t(opposite(d))]);
            }
    }

private:
    static void hang(DecoratorContext& c, BlockPos p, StateId vine) {
        c.setBlock(p, vine);
        p = p.below();
        for (int i = 4; i > 0 && c.isAir(p); --i) {
            c.setBlock(p, vine);
            p = p.below();
        }
    }
    VineStates vines_;
    float probability_;
};

// Cocoa pods on the lowest logs of jungle trees.
class CocoaDecorator final : public TreeDecorator {
public:
    CocoaDecorator(const BlockRegistry& reg, float probability) : probability_(probability) {
        const StateId base = reg.defaultState("cocoa");
        for (Direction d : kHorizontal) {
            const StateId facing = withProp(reg, base, "facing", directionName(d));
            for (int age = 0; age < 3; ++age) pods_[size_t(d)][size_t(age)] = withProp(reg, facing, "age", age);
        }
    }
    void place(DecoratorContext& c) const override {
        if (c.random.nextFloat() >= probability_ || c.logs.empty()) return;
        const int y0 = c.logs.front().y;
        for (const BlockPos& log : c.logs) {
            if (log.y - y0 > 2) continue;
            for (Direction d : kHorizontal) {
                if (c.random.nextFloat() > 0.25f) continue;
                // The pod faces the log it hangs on.
                const BlockPos p = log.offset(opposite(d));
                if (c.isAir(p)) c.setBlock(p, pods_[size_t(d)][size_t(c.random.nextInt(3))]);
            }
        }
    }

private:
    float probability_;
    std::array<std::array<StateId, 3>, 6> pods_{};
};

// A bee nest (without bees: no entities yet) on the side of the trunk just
// under the leaves, facing south into free air.
class BeehiveDecorator final : public TreeDecorator {
public:
    BeehiveDecorator(const BlockRegistry& reg, float probability)
        : probability_(probability), nest_(withProp(reg, reg.defaultState("bee_nest"), "facing", kNestFacing)) {}
    void place(DecoratorContext& c) const override {
        if (c.logs.empty() || c.random.nextFloat() >= probability_) return;
        const int y = !c.leaves.empty() ? std::max(c.leaves.front().y - 1, c.logs.front().y + 1)
                                        : std::min(c.logs.front().y + 1 + c.random.nextInt(3), c.logs.back().y);
        std::vector<BlockPos> spots;
        for (const BlockPos& log : c.logs)
            if (log.y == y)
                for (Direction d : {Direction::East, Direction::South, Direction::West}) spots.push_back(log.offset(d));
        if (spots.empty()) return;
        shuffle(spots, c.random);
        for (const BlockPos& p : spots)
            if (c.isAir(p) && c.isAir(p.offset(Direction::South))) {
                c.setBlock(p, nest_);
                return;
            }
    }

private:
    static constexpr std::string_view kNestFacing = "south";
    float probability_;
    StateId nest_;
};

// Vanilla TreeFeature.getLowestTrunkOrRootOfTree: the logs, the roots, or (when
// both start at the same height) both, in `both`.
std::span<const BlockPos> lowestTrunkOrRoots(const DecoratorContext& c, std::vector<BlockPos>& both) {
    if (c.roots.empty()) return c.logs;
    if (c.logs.empty() || c.roots.front().y != c.logs.front().y) return c.roots;
    both.assign(c.logs.begin(), c.logs.end());
    both.insert(both.end(), c.roots.begin(), c.roots.end());
    return both;
}

// Podzol (or the provider's block) in blotches around giant spruce and pine.
class AlterGroundDecorator final : public TreeDecorator {
public:
    AlterGroundDecorator(const BlockRegistry& reg, BlockStateProvider provider)
        : provider_(provider), dirt_(reg.tag("dirt").value_or(TagId(0))) {}
    void place(DecoratorContext& c) const override {
        std::vector<BlockPos> both;
        const std::span<const BlockPos> list = lowestTrunkOrRoots(c, both);
        if (list.empty()) return;
        const int y0 = list.front().y;
        for (const BlockPos& p : list) {
            if (p.y != y0) continue;
            circle(c, p.offset(-1, 0, -1));
            circle(c, p.offset(2, 0, -1));
            circle(c, p.offset(-1, 0, 2));
            circle(c, p.offset(2, 0, 2));
            for (int i = 0; i < 5; ++i) {
                const int k = c.random.nextInt(64);
                const int x = k % 8, z = k / 8;
                if (x == 0 || x == 7 || z == 0 || z == 7) circle(c, p.offset(-3 + x, 0, -3 + z));
            }
        }
    }

private:
    void circle(DecoratorContext& c, const BlockPos& center) const {
        for (int dx = -2; dx <= 2; ++dx)
            for (int dz = -2; dz <= 2; ++dz)
                if (std::abs(dx) != 2 || std::abs(dz) != 2) column(c, center.offset(dx, 0, dz));
    }
    // The first dirt-like block looking down from 2 above; below `pos` only through air.
    void column(DecoratorContext& c, const BlockPos& pos) const {
        for (int dy = 2; dy >= -3; --dy) {
            const BlockPos p = pos.above(dy);
            if (c.level.hasTag(p, dirt_)) {
                c.setBlock(p, provider_.get(c.random, pos));
                return;
            }
            if (!c.isAir(p) && dy < 0) return;
        }
    }
    BlockStateProvider provider_;
    TagId dirt_;
};

// 1.21.5: blocks (leaf litter) scattered on the open ground around the trunk.
class PlaceOnGroundDecorator final : public TreeDecorator {
public:
    PlaceOnGroundDecorator(const BlockRegistry& reg, int tries, int radius, int height, BlockStateProvider provider)
        : tries_(tries), radius_(radius), height_(height), provider_(provider), vine_(VineStates(reg).block) {}
    void place(DecoratorContext& c) const override {
        std::vector<BlockPos> both;
        const std::span<const BlockPos> list = lowestTrunkOrRoots(c, both);
        if (list.empty()) return;
        const BlockPos first = list.front();
        int x0 = first.x, x1 = first.x, z0 = first.z, z1 = first.z;
        for (const BlockPos& p : list)
            if (p.y == first.y) {
                x0 = std::min(x0, p.x);
                x1 = std::max(x1, p.x);
                z0 = std::min(z0, p.z);
                z1 = std::max(z1, p.z);
            }
        for (int i = 0; i < tries_; ++i) {
            const int x = c.random.nextIntBetween(x0 - radius_, x1 + radius_);
            const int y = c.random.nextIntBetween(first.y - height_, first.y + height_);
            const int z = c.random.nextIntBetween(z0 - radius_, z1 + radius_);
            attempt(c, {x, y, z});
        }
    }

private:
    void attempt(DecoratorContext& c, const BlockPos& ground) const {
        const BlockPos p = ground.above();
        const StateInfo& above = c.level.info(p);
        if (!(above.flags & kAir) && above.block != vine_) return;
        if (!(c.level.info(ground).flags & kOpaqueCube)) return;
        if (c.level.height(Heightmap::MotionBlockingNoLeaves, ground.x, ground.z) > p.y) return;
        c.setBlock(p, provider_.get(c.random, p));
    }
    int tries_, radius_, height_;
    BlockStateProvider provider_;
    uint16_t vine_;
};

// 1.21.5: blocks next to logs in one of the given directions (mushrooms on top
// of fallen logs).
class AttachedToLogsDecorator final : public TreeDecorator {
public:
    AttachedToLogsDecorator(float probability, BlockStateProvider provider, std::vector<Direction> directions)
        : probability_(probability), provider_(provider), directions_(std::move(directions)) {}
    void place(DecoratorContext& c) const override {
        std::vector<BlockPos> logs(c.logs.begin(), c.logs.end());
        shuffle(logs, c.random);
        for (const BlockPos& log : logs) {
            const Direction d = directions_[size_t(c.random.nextInt(int(directions_.size())))];
            const BlockPos p = log.offset(d);
            if (c.random.nextFloat() <= probability_ && c.isAir(p)) c.setBlock(p, provider_.get(c.random, p));
        }
    }

private:
    float probability_;
    BlockStateProvider provider_;
    std::vector<Direction> directions_;
};

std::unique_ptr<TreeDecorator> parseDecorator(const Json& j, FeatureLoader& l) {
    const std::string type = treeTypeOf(j);
    const BlockRegistry& reg = l.registry;
    auto provider = [&](const char* key) -> std::optional<BlockStateProvider> {
        if (!j.contains(key)) {
            l.error("decorator " + type + " needs '" + key + "'");
            return std::nullopt;
        }
        return BlockStateProvider::parse(j[key], l);
    };
    if (type == "trunk_vine") return std::make_unique<TrunkVineDecorator>(reg);
    if (type == "leave_vine") return std::make_unique<LeaveVineDecorator>(reg, l.getFloat(j, "probability", 0.0f));
    if (type == "cocoa") return std::make_unique<CocoaDecorator>(reg, l.getFloat(j, "probability", 0.0f));
    if (type == "beehive") return std::make_unique<BeehiveDecorator>(reg, l.getFloat(j, "probability", 0.0f));
    if (type == "alter_ground") {
        auto p = provider("provider");
        if (!p) return nullptr;
        return std::make_unique<AlterGroundDecorator>(reg, *p);
    }
    if (type == "place_on_ground") {
        auto p = provider("block_state_provider");
        if (!p) return nullptr;
        return std::make_unique<PlaceOnGroundDecorator>(reg, l.getInt(j, "tries", 128), l.getInt(j, "radius", 2),
                                                        l.getInt(j, "height", 1), *p);
    }
    if (type == "attached_to_logs") {
        auto p = provider("block_provider");
        if (!p) return nullptr;
        std::vector<Direction> dirs;
        for (const Json& d : j.value("directions", Json::array())) {
            auto dir = d.is_string() ? directionFromName(d.get<std::string>()) : std::nullopt;
            if (!dir) {
                l.error("bad direction " + d.dump());
                return nullptr;
            }
            dirs.push_back(*dir);
        }
        if (dirs.empty()) {
            l.error("attached_to_logs needs 'directions'");
            return nullptr;
        }
        return std::make_unique<AttachedToLogsDecorator>(l.getFloat(j, "probability", 0.0f), *p, std::move(dirs));
    }
    l.error("unknown tree decorator '" + type + "'");
    return nullptr;
}

} // namespace

bool parseTreeDecorators(const Json& j, FeatureLoader& l, std::vector<std::unique_ptr<TreeDecorator>>& out) {
    if (!j.is_array()) {
        l.error("decorators should be a list");
        return false;
    }
    for (const Json& d : j) {
        auto decorator = parseDecorator(d, l);
        if (!decorator) return false;
        out.push_back(std::move(decorator));
    }
    return true;
}

} // namespace mc
