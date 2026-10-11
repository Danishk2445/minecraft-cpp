// TreePlacers.cpp -- vanilla trunk placers (straight, forking, giant, mega
// jungle, dark oak, fancy, bending, cherry), foliage placers (blob, spruce,
// pine, mega pine, acacia, bush, fancy, jungle, dark oak, random spread,
// cherry) and feature sizes.
#include <cmath>

#include "world/gen/feature/Feature.h"
#include "world/gen/feature/TreeConfig.h"

namespace mc {
namespace {

constexpr double kPi = 3.14159265358979323846;

// Vanilla Direction.Plane.HORIZONTAL.getRandomDirection (order N, E, S, W).
Direction randomHorizontal(Xoroshiro& r) { return kHorizontal[size_t(r.nextInt(4))]; }

int manhattan(const BlockPos& a, const BlockPos& b) {
    return std::abs(a.x - b.x) + std::abs(a.y - b.y) + std::abs(a.z - b.z);
}

int floorToInt(float v) { return int(std::floor(v)); }

// ---------------------------------------------------------------- trunk placers

class StraightTrunkPlacer final : public TrunkPlacer {
public:
    void placeTrunk(TreeContext& t, int height, const BlockPos& pos, Attachments& out) const override {
        t.setDirtAt(pos.below());
        for (int i = 0; i < height; ++i) t.placeLog(pos.above(i));
        out.push_back({pos.above(height), 0, false});
    }
};

// Acacia: the top 1-3 blocks lean one way; a second branch may fork off lower.
class ForkingTrunkPlacer final : public TrunkPlacer {
public:
    void placeTrunk(TreeContext& t, int height, const BlockPos& pos, Attachments& out) const override {
        Xoroshiro& r = t.random;
        t.setDirtAt(pos.below());
        const Direction lean = randomHorizontal(r);
        const int forkAt = height - r.nextInt(4) - 1;
        int steps = 3 - r.nextInt(3);
        int x = pos.x, z = pos.z;
        std::optional<int> top;
        for (int i = 0; i < height; ++i) {
            if (i >= forkAt && steps > 0) {
                x += normal(lean).x;
                z += normal(lean).z;
                --steps;
            }
            if (t.placeLog({x, pos.y + i, z})) top = pos.y + i + 1;
        }
        if (top) out.push_back({{x, *top, z}, 1, false});

        x = pos.x;
        z = pos.z;
        const Direction branch = randomHorizontal(r);
        if (branch == lean) return;
        const int start = forkAt - r.nextInt(2) - 1;
        int length = 1 + r.nextInt(3);
        top.reset();
        for (int i = start; i < height && length > 0; ++i, --length) {
            if (i < 1) continue;
            x += normal(branch).x;
            z += normal(branch).z;
            if (t.placeLog({x, pos.y + i, z})) top = pos.y + i + 1;
        }
        if (top) out.push_back({{x, *top, z}, 0, false});
    }
};

// 2x2 trunks (giant spruce and pine); the crown sits on a "double trunk".
class GiantTrunkPlacer : public TrunkPlacer {
public:
    void placeTrunk(TreeContext& t, int height, const BlockPos& pos, Attachments& out) const override {
        const BlockPos below = pos.below();
        t.setDirtAt(below);
        t.setDirtAt(below.offset(1, 0, 0));
        t.setDirtAt(below.offset(0, 0, 1));
        t.setDirtAt(below.offset(1, 0, 1));
        // The top block is single: the crown's center column.
        for (int i = 0; i < height; ++i) {
            t.placeLogIfFree(pos.offset(0, i, 0));
            if (i < height - 1) {
                t.placeLogIfFree(pos.offset(1, i, 0));
                t.placeLogIfFree(pos.offset(1, i, 1));
                t.placeLogIfFree(pos.offset(0, i, 1));
            }
        }
        out.push_back({pos.above(height), 0, true});
    }
};

// A giant trunk plus side branches every 2-5 blocks, each with a small crown.
class MegaJungleTrunkPlacer final : public GiantTrunkPlacer {
public:
    void placeTrunk(TreeContext& t, int height, const BlockPos& pos, Attachments& out) const override {
        GiantTrunkPlacer::placeTrunk(t, height, pos, out);
        Xoroshiro& r = t.random;
        for (int i = height - 2 - r.nextInt(4); i > height / 2; i -= 2 + r.nextInt(4)) {
            const float angle = r.nextFloat() * float(kPi * 2.0);
            int dx = 0, dz = 0;
            for (int l = 0; l < 5; ++l) {
                dx = int(1.5f + std::cos(angle) * float(l));
                dz = int(1.5f + std::sin(angle) * float(l));
                t.placeLog(pos.offset(dx, i - 3 + l / 2, dz));
            }
            out.push_back({pos.offset(dx, i, dz), -2, false});
        }
    }
};

// 2x2 trunk that may lean near the top, with 2-4 block log stubs hanging
// under the canopy around it, each carrying its own patch of leaves.
class DarkOakTrunkPlacer final : public TrunkPlacer {
public:
    void placeTrunk(TreeContext& t, int height, const BlockPos& pos, Attachments& out) const override {
        Xoroshiro& r = t.random;
        const BlockPos below = pos.below();
        t.setDirtAt(below);
        t.setDirtAt(below.offset(1, 0, 0));
        t.setDirtAt(below.offset(0, 0, 1));
        t.setDirtAt(below.offset(1, 0, 1));
        const Direction lean = randomHorizontal(r);
        const int leanAt = height - r.nextInt(4);
        int steps = 2 - r.nextInt(3);
        int x = pos.x, z = pos.z;
        const int top = pos.y + height - 1;
        for (int i = 0; i < height; ++i) {
            if (i >= leanAt && steps > 0) {
                x += normal(lean).x;
                z += normal(lean).z;
                --steps;
            }
            const BlockPos p(x, pos.y + i, z);
            if (t.isAirOrLeaves(p)) {
                t.placeLog(p);
                t.placeLog(p.offset(1, 0, 0));
                t.placeLog(p.offset(0, 0, 1));
                t.placeLog(p.offset(1, 0, 1));
            }
        }
        out.push_back({{x, top, z}, 0, true});
        for (int ox = -1; ox <= 2; ++ox)
            for (int oz = -1; oz <= 2; ++oz) {
                if ((ox >= 0 && ox <= 1 && oz >= 0 && oz <= 1) || r.nextInt(3) > 0) continue;
                const int length = r.nextInt(3) + 2;
                for (int k = 0; k < length; ++k) t.placeLog({pos.x + ox, top - k - 1, pos.z + oz});
                out.push_back({{x + ox, top, z + oz}, 0, false});
            }
    }
};

// Big oak: a trunk to 0.618 of the height and sloped branches ending in leaf
// clusters (vanilla's port of the old BigTree generator).
class FancyTrunkPlacer final : public TrunkPlacer {
public:
    void placeTrunk(TreeContext& t, int height, const BlockPos& pos, Attachments& out) const override {
        Xoroshiro& r = t.random;
        const int total = height + 2;
        const int trunkTop = int(std::floor(double(total) * 0.618));
        t.setDirtAt(pos.below());
        const int clusters = std::min(1, int(std::floor(1.382 + std::pow(double(total) / 13.0, 2.0))));
        const int maxBase = pos.y + trunkTop;
        struct Cluster {
            BlockPos end;
            int branchBase;
        };
        std::vector<Cluster> list;
        list.push_back({pos.above(total - 5), maxBase});
        for (int y = total - 5; y >= 0; --y) {
            const float shape = treeShape(total, y);
            if (shape < 0.0f) continue;
            for (int k = 0; k < clusters; ++k) {
                const double length = double(shape) * (double(r.nextFloat()) + 0.328);
                const double angle = double(r.nextFloat() * 2.0f) * kPi;
                const double dx = length * std::sin(angle) + 0.5, dz = length * std::cos(angle) + 0.5;
                const BlockPos end = pos.offset(int(std::floor(dx)), y - 1, int(std::floor(dz)));
                if (!limb(t, end, end.above(5), false)) continue;
                // The branch rises from the trunk at slope 0.381.
                const int ox = pos.x - end.x, oz = pos.z - end.z;
                const double baseY = double(end.y) - std::sqrt(double(ox * ox + oz * oz)) * 0.381;
                const int base = baseY > double(maxBase) ? maxBase : int(baseY);
                if (limb(t, {pos.x, base, pos.z}, end, false)) list.push_back({end, base});
            }
        }
        limb(t, pos, pos.above(trunkTop), true);
        for (const Cluster& c : list) {
            const BlockPos start(pos.x, c.branchBase, pos.z);
            if (start != c.end && keepBranch(total, c.branchBase - pos.y)) limb(t, start, c.end, true);
        }
        for (const Cluster& c : list)
            if (keepBranch(total, c.branchBase - pos.y)) out.push_back({c.end, 0, false});
    }

private:
    // Logs along the line a -> b with the axis of the line's main direction, or
    // (when not placing) whether the whole line is free.
    static bool limb(TreeContext& t, const BlockPos& a, const BlockPos& b, bool place) {
        if (!place && a == b) return true;
        const int dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
        const int steps = std::max({std::abs(dx), std::abs(dy), std::abs(dz)});
        if (steps == 0) { // vanilla divides 0/0 here and lands on `a`
            if (place) t.placeLog(a, 1);
            return true;
        }
        const float fx = float(dx) / float(steps), fy = float(dy) / float(steps), fz = float(dz) / float(steps);
        for (int j = 0; j <= steps; ++j) {
            const BlockPos p = a.offset(floorToInt(0.5f + float(j) * fx), floorToInt(0.5f + float(j) * fy),
                                        floorToInt(0.5f + float(j) * fz));
            if (place) t.placeLog(p, logAxis(a, p));
            else if (!t.isFree(p)) return false;
        }
        return true;
    }

    static int logAxis(const BlockPos& a, const BlockPos& b) {
        const int ax = std::abs(b.x - a.x), az = std::abs(b.z - a.z);
        const int m = std::max(ax, az);
        if (m == 0) return 1;
        return ax == m ? 0 : 2;
    }

    // Branch length scale at height y: a sphere-ish envelope over the top 70%.
    static float treeShape(int height, int y) {
        if (float(y) < float(height) * 0.3f) return -1.0f;
        const float half = float(height) / 2.0f;
        const float d = half - float(y);
        if (d == 0.0f) return half * 0.5f;
        if (std::abs(d) >= half) return 0.0f;
        return std::sqrt(half * half - d * d) * 0.5f;
    }

    static bool keepBranch(int height, int y) { return double(y) >= double(height) * 0.2; }
};

// Azalea: a trunk that bends sideways at the top; leaves hang off every block
// from min_height_for_leaves up.
class BendingTrunkPlacer final : public TrunkPlacer {
public:
    int minHeightForLeaves = 1;
    IntProvider bendLength;

    void placeTrunk(TreeContext& t, int height, const BlockPos& pos, Attachments& out) const override {
        Xoroshiro& r = t.random;
        const Direction dir = randomHorizontal(r);
        const int top = height - 1;
        BlockPos p = pos;
        t.setDirtAt(p.below());
        for (int j = 0; j <= top; ++j) {
            if (j + 1 >= top + r.nextInt(2)) p = p.offset(dir);
            if (t.validTreePos(p)) t.placeLog(p);
            if (j >= minHeightForLeaves) out.push_back({p, 0, false});
            p = p.above();
        }
        const int bend = bendLength.sample(r);
        for (int k = 0; k <= bend; ++k) {
            if (t.validTreePos(p)) t.placeLog(p);
            out.push_back({p, 0, false});
            p = p.offset(dir);
        }
    }
};

// Cherry: a short trunk splitting into 1-3 branches that bend out and up
// (two opposite ones; with three the trunk itself goes on to the top).
class CherryTrunkPlacer final : public TrunkPlacer {
public:
    IntProvider branchCount, branchLength, branchStart, secondBranchStart, branchEnd;

    void placeTrunk(TreeContext& t, int height, const BlockPos& pos, Attachments& out) const override {
        Xoroshiro& r = t.random;
        t.setDirtAt(pos.below());
        const int first = std::max(0, height - 1 + branchStart.sample(r));
        int second = std::max(0, height - 1 + secondBranchStart.sample(r));
        if (second >= first) ++second;
        const int count = branchCount.sample(r);
        const bool three = count == 3, two = count >= 2;
        const int trunk = three ? height : two ? std::max(first, second) + 1 : first + 1;
        for (int i = 0; i < trunk; ++i) t.placeLog(pos.above(i));
        if (three) out.push_back({pos.above(trunk), 0, false});
        const Direction dir = randomHorizontal(r);
        const int axis = int(axisOf(dir));
        out.push_back(branch(t, height, pos, axis, dir, first, first < trunk - 1));
        if (two) out.push_back(branch(t, height, pos, axis, opposite(dir), second, second < trunk - 1));
    }

private:
    FoliageAttachment branch(TreeContext& t, int height, const BlockPos& pos, int axis, Direction dir, int start,
                             bool doubleBranch) const {
        Xoroshiro& r = t.random;
        BlockPos cur = pos.above(start);
        const int endY = height - 1 + branchEnd.sample(r);
        const bool extend = doubleBranch || endY < start;
        const int length = branchLength.sample(r) + (extend ? 1 : 0);
        const BlockPos end = pos.offset(dir, length).above(endY);
        for (int i = 0; i < (extend ? 2 : 1); ++i) {
            cur = cur.offset(dir);
            t.placeLog(cur, axis);
        }
        const Direction vertical = end.y > cur.y ? Direction::Up : Direction::Down;
        // Walk to the end, rising (or falling) with the probability that keeps the slope even.
        for (int guard = 0; guard < 64; ++guard) {
            const int dist = manhattan(cur, end);
            if (dist == 0) break;
            const float f = float(std::abs(end.y - cur.y)) / float(dist);
            const bool up = r.nextFloat() < f;
            cur = cur.offset(up ? vertical : dir);
            t.placeLog(cur, up ? -1 : axis);
        }
        return {end.above(), 0, false};
    }
};

// ---------------------------------------------------------------- foliage placers

class BlobFoliagePlacer : public FoliagePlacer {
public:
    int height = 3;
    int foliageHeight(Xoroshiro&, int) const override { return height; }

protected:
    void place(TreeContext& t, const FoliageAttachment& a, int h, int radius, int offset) const override {
        for (int y = offset; y >= offset - h; --y)
            placeLeavesRow(t, a.pos, std::max(radius + a.radiusOffset - 1 - y / 2, 0), y, a.doubleTrunk);
    }
    bool skip(Xoroshiro& r, int dx, int y, int dz, int range, bool) const override {
        return dx == range && dz == range && (r.nextInt(2) == 0 || y == 0);
    }
};

// Jungle bushes: a squat dome.
class BushFoliagePlacer final : public BlobFoliagePlacer {
protected:
    void place(TreeContext& t, const FoliageAttachment& a, int h, int radius, int offset) const override {
        for (int y = offset; y >= offset - h; --y)
            placeLeavesRow(t, a.pos, radius + a.radiusOffset - 1 - y, y, a.doubleTrunk);
    }
    bool skip(Xoroshiro& r, int dx, int, int dz, int range, bool) const override {
        return dx == range && dz == range && r.nextInt(2) == 0;
    }
};

// Big oak clusters: round blobs above each branch end.
class FancyFoliagePlacer final : public BlobFoliagePlacer {
protected:
    void place(TreeContext& t, const FoliageAttachment& a, int h, int radius, int offset) const override {
        for (int y = offset; y >= offset - h; --y)
            placeLeavesRow(t, a.pos, radius + (y != offset && y != offset - h ? 1 : 0), y, a.doubleTrunk);
    }
    bool skip(Xoroshiro&, int dx, int, int dz, int range, bool) const override {
        const float x = float(dx) + 0.5f, z = float(dz) + 0.5f;
        return x * x + z * z > float(range * range);
    }
};

// Spruce: rings that grow and shrink down the trunk.
class SpruceFoliagePlacer final : public FoliagePlacer {
public:
    IntProvider trunkHeight;
    int foliageHeight(Xoroshiro& r, int treeHeight) const override {
        return std::max(4, treeHeight - trunkHeight.sample(r));
    }

protected:
    void place(TreeContext& t, const FoliageAttachment& a, int h, int radius, int offset) const override {
        int range = t.random.nextInt(2), limit = 1, next = 0;
        for (int y = offset; y >= -h; --y) {
            placeLeavesRow(t, a.pos, range, y, a.doubleTrunk);
            if (range >= limit) {
                range = next;
                next = 1;
                limit = std::min(limit + 1, radius + a.radiusOffset);
            } else {
                ++range;
            }
        }
    }
    bool skip(Xoroshiro&, int dx, int, int dz, int range, bool) const override {
        return dx == range && dz == range && range > 0;
    }
};

// Pine: a narrow cone with a pinched bottom ring.
class PineFoliagePlacer final : public FoliagePlacer {
public:
    IntProvider height;
    int foliageHeight(Xoroshiro& r, int) const override { return height.sample(r); }
    int foliageRadius(Xoroshiro& r, int trunkHeight) const override {
        const int base = radius.sample(r);
        return base + r.nextInt(std::max(trunkHeight + 1, 1));
    }

protected:
    void place(TreeContext& t, const FoliageAttachment& a, int h, int radius, int offset) const override {
        int range = 0;
        for (int y = offset; y >= offset - h; --y) {
            placeLeavesRow(t, a.pos, range, y, a.doubleTrunk);
            if (range >= 1 && y == offset - h + 1) --range;
            else if (range < radius + a.radiusOffset) ++range;
        }
    }
    bool skip(Xoroshiro&, int dx, int, int dz, int range, bool) const override {
        return dx == range && dz == range && range > 0;
    }
};

// Giant spruce and pine: a round cone widening downward, ragged on even rows.
class MegaPineFoliagePlacer final : public FoliagePlacer {
public:
    IntProvider crownHeight;
    int foliageHeight(Xoroshiro& r, int) const override { return crownHeight.sample(r); }

protected:
    void place(TreeContext& t, const FoliageAttachment& a, int h, int radius, int offset) const override {
        const BlockPos& c = a.pos;
        int previous = 0;
        for (int y = c.y - h + offset; y <= c.y + offset; ++y) {
            const int down = c.y - y;
            const int base = radius + a.radiusOffset + (h > 0 ? floorToInt(float(down) / float(h) * 3.5f) : 0);
            const int range = down > 0 && base == previous && (y & 1) == 0 ? base + 1 : base;
            placeLeavesRow(t, {c.x, y, c.z}, range, 0, a.doubleTrunk);
            previous = base;
        }
    }
    bool skip(Xoroshiro&, int dx, int, int dz, int range, bool) const override {
        return dx + dz >= 7 || dx * dx + dz * dz > range * range;
    }
};

// Acacia: a wide flat layer under a plus-shaped one.
class AcaciaFoliagePlacer final : public FoliagePlacer {
public:
    int foliageHeight(Xoroshiro&, int) const override { return 0; }

protected:
    void place(TreeContext& t, const FoliageAttachment& a, int h, int radius, int offset) const override {
        const BlockPos c = a.pos.above(offset);
        placeLeavesRow(t, c, radius + a.radiusOffset, -1 - h, a.doubleTrunk);
        placeLeavesRow(t, c, radius - 1, -h, a.doubleTrunk);
        placeLeavesRow(t, c, radius + a.radiusOffset - 1, 0, a.doubleTrunk);
    }
    bool skip(Xoroshiro&, int dx, int y, int dz, int range, bool) const override {
        if (y == 0) return (dx > 1 || dz > 1) && dx != 0 && dz != 0;
        return dx == range && dz == range && range > 0;
    }
};

// Mega jungle crowns: round layers widening downward; side branches get 2-3.
class JungleFoliagePlacer final : public FoliagePlacer {
public:
    int height = 2;
    int foliageHeight(Xoroshiro&, int) const override { return height; }

protected:
    void place(TreeContext& t, const FoliageAttachment& a, int h, int radius, int offset) const override {
        const int layers = a.doubleTrunk ? h : 1 + t.random.nextInt(2);
        for (int y = offset; y >= offset - layers; --y)
            placeLeavesRow(t, a.pos, radius + a.radiusOffset + 1 - y, y, a.doubleTrunk);
    }
    bool skip(Xoroshiro&, int dx, int, int dz, int range, bool) const override {
        return dx + dz >= 7 || dx * dx + dz * dz > range * range;
    }
};

class DarkOakFoliagePlacer final : public FoliagePlacer {
public:
    int foliageHeight(Xoroshiro&, int) const override { return 4; }

protected:
    void place(TreeContext& t, const FoliageAttachment& a, int, int radius, int offset) const override {
        const BlockPos c = a.pos.above(offset);
        const bool large = a.doubleTrunk;
        if (large) {
            placeLeavesRow(t, c, radius + 2, -1, large);
            placeLeavesRow(t, c, radius + 3, 0, large);
            placeLeavesRow(t, c, radius + 2, 1, large);
            if (t.random.nextBool()) placeLeavesRow(t, c, radius, 2, large);
        } else {
            placeLeavesRow(t, c, radius + 2, -1, large);
            placeLeavesRow(t, c, radius + 1, 0, large);
        }
    }
    bool skipSigned(Xoroshiro& r, int dx, int y, int dz, int range, bool large) const override {
        // The wide middle layer of the main crown loses its corners.
        if (y == 0 && large && (dx == -range || dx >= range) && (dz == -range || dz >= range)) return true;
        return FoliagePlacer::skipSigned(r, dx, y, dz, range, large);
    }
    bool skip(Xoroshiro&, int dx, int y, int dz, int range, bool large) const override {
        if (y == -1 && !large) return dx == range && dz == range;
        return y == 1 && dx + dz > range * 2 - 2;
    }
};

// Azalea: leaves scattered at random around every attachment.
class RandomSpreadFoliagePlacer final : public FoliagePlacer {
public:
    IntProvider height;
    int attempts = 128;
    int foliageHeight(Xoroshiro& r, int) const override { return height.sample(r); }

protected:
    void place(TreeContext& t, const FoliageAttachment& a, int h, int radius, int) const override {
        if (radius <= 0 || h <= 0) return;
        Xoroshiro& r = t.random;
        for (int i = 0; i < attempts; ++i) {
            const int x0 = r.nextInt(radius), x1 = r.nextInt(radius);
            const int y0 = r.nextInt(h), y1 = r.nextInt(h);
            const int z0 = r.nextInt(radius), z1 = r.nextInt(radius);
            t.tryPlaceLeaf(a.pos.offset(x0 - x1, y0 - y1, z0 - z1));
        }
    }
    bool skip(Xoroshiro&, int, int, int, int, bool) const override { return false; }
};

// Cherry: a wide rounded crown with holes and leaves hanging below its rim.
class CherryFoliagePlacer final : public FoliagePlacer {
public:
    IntProvider height;
    float wideBottomHole = 0.0f, cornerHole = 0.0f, hanging = 0.0f, hangingExtension = 0.0f;
    int foliageHeight(Xoroshiro& r, int) const override { return height.sample(r); }

protected:
    void place(TreeContext& t, const FoliageAttachment& a, int h, int radius, int offset) const override {
        const bool large = a.doubleTrunk;
        const BlockPos c = a.pos.above(offset);
        const int range = radius + a.radiusOffset - 1;
        placeLeavesRow(t, c, range - 2, h - 3, large);
        placeLeavesRow(t, c, range - 1, h - 4, large);
        for (int y = h - 5; y >= 0; --y) placeLeavesRow(t, c, range, y, large);
        placeLeavesRowWithHangingLeavesBelow(t, c, range, -1, large, hanging, hangingExtension);
        placeLeavesRowWithHangingLeavesBelow(t, c, range - 1, -2, large, hanging, hangingExtension);
    }
    bool skip(Xoroshiro& r, int dx, int y, int dz, int range, bool) const override {
        if (y == -1 && (dx == range || dz == range) && r.nextFloat() < wideBottomHole) return true;
        const bool corner = dx == range && dz == range;
        if (range > 2) return corner || (dx + dz > range * 2 - 2 && r.nextFloat() < cornerHole);
        return corner && r.nextFloat() < cornerHole;
    }
};

bool tryPlaceExtension(TreeContext& t, float chance, const BlockPos& logPos, const BlockPos& p) {
    if (manhattan(p, logPos) >= 7) return false;
    if (t.random.nextFloat() > chance) return false;
    return t.tryPlaceLeaf(p);
}

} // namespace

// ---------------------------------------------------------------- FoliagePlacer

bool FoliagePlacer::skipSigned(Xoroshiro& r, int dx, int y, int dz, int range, bool large) const {
    // Double-trunk crowns are centered between columns 0 and 1.
    const int ax = large ? std::min(std::abs(dx), std::abs(dx - 1)) : std::abs(dx);
    const int az = large ? std::min(std::abs(dz), std::abs(dz - 1)) : std::abs(dz);
    return skip(r, ax, y, az, range, large);
}

void FoliagePlacer::placeLeavesRow(TreeContext& t, const BlockPos& center, int range, int y, bool large) const {
    const int extra = large ? 1 : 0;
    for (int dx = -range; dx <= range + extra; ++dx)
        for (int dz = -range; dz <= range + extra; ++dz)
            if (!skipSigned(t.random, dx, y, dz, range, large)) t.tryPlaceLeaf(center.offset(dx, y, dz));
}

void FoliagePlacer::placeLeavesRowWithHangingLeavesBelow(TreeContext& t, const BlockPos& center, int range, int y,
                                                         bool large, float chance, float extensionChance) const {
    placeLeavesRow(t, center, range, y, large);
    const int extra = large ? 1 : 0;
    const BlockPos logPos = center.below();
    // Walk the row's rim, each side in turn; under rim leaves of this tree hang 1-2 more.
    for (Direction d : kHorizontal) {
        const Direction side = rotateYClockwise(d);
        const int out = isPositive(side) ? range + extra : range;
        BlockPos p = center.offset(0, y - 1, 0).offset(side, out).offset(d, -range);
        for (int k = -range; k < range + extra; ++k) {
            if (t.foliageAt(p.above()) && tryPlaceExtension(t, chance, logPos, p))
                tryPlaceExtension(t, extensionChance, logPos, p.below());
            p = p.offset(d);
        }
    }
}

// ---------------------------------------------------------------- parsing

std::optional<FeatureSize> FeatureSize::parse(const Json& j, FeatureLoader& l) {
    FeatureSize s;
    const std::string type = treeTypeOf(j);
    if (type == "two_layers_feature_size") {
        s.limit = l.getInt(j, "limit", 1);
        s.lowerSize = l.getInt(j, "lower_size", 0);
        s.upperSize = l.getInt(j, "upper_size", 1);
    } else if (type == "three_layers_feature_size") {
        s.threeLayers = true;
        s.limit = l.getInt(j, "limit", 1);
        s.upperLimit = l.getInt(j, "upper_limit", 1);
        s.lowerSize = l.getInt(j, "lower_size", 0);
        s.middleSize = l.getInt(j, "middle_size", 1);
        s.upperSize = l.getInt(j, "upper_size", 1);
    } else {
        l.error("unknown feature size '" + type + "'");
        return std::nullopt;
    }
    if (j.contains("min_clipped_height")) s.minClippedHeight = l.getInt(j, "min_clipped_height", 0);
    return s;
}

std::unique_ptr<TrunkPlacer> parseTrunkPlacer(const Json& j, FeatureLoader& l) {
    const std::string type = treeTypeOf(j);
    auto intProvider = [&](const char* key, std::optional<IntProvider>& out) {
        if (!j.contains(key)) {
            l.error(std::string("trunk placer needs '") + key + "'");
            return false;
        }
        out = IntProvider::parse(j[key], l);
        return out.has_value();
    };
    std::unique_ptr<TrunkPlacer> p;
    if (type == "straight_trunk_placer") {
        p = std::make_unique<StraightTrunkPlacer>();
    } else if (type == "forking_trunk_placer") {
        p = std::make_unique<ForkingTrunkPlacer>();
    } else if (type == "giant_trunk_placer") {
        p = std::make_unique<GiantTrunkPlacer>();
    } else if (type == "mega_jungle_trunk_placer") {
        p = std::make_unique<MegaJungleTrunkPlacer>();
    } else if (type == "dark_oak_trunk_placer") {
        p = std::make_unique<DarkOakTrunkPlacer>();
    } else if (type == "fancy_trunk_placer") {
        p = std::make_unique<FancyTrunkPlacer>();
    } else if (type == "bending_trunk_placer") {
        auto b = std::make_unique<BendingTrunkPlacer>();
        b->minHeightForLeaves = l.getInt(j, "min_height_for_leaves", 1);
        std::optional<IntProvider> bend;
        if (!intProvider("bend_length", bend)) return nullptr;
        b->bendLength = *bend;
        p = std::move(b);
    } else if (type == "cherry_trunk_placer") {
        auto c = std::make_unique<CherryTrunkPlacer>();
        std::optional<IntProvider> count, length, start, end;
        if (!intProvider("branch_count", count) || !intProvider("branch_horizontal_length", length) ||
            !intProvider("branch_start_offset_from_top", start) || !intProvider("branch_end_offset_from_top", end))
            return nullptr;
        c->branchCount = *count;
        c->branchLength = *length;
        c->branchStart = *start;
        // Vanilla: the second branch starts from one lower at most.
        const int lo = start->minValue();
        c->secondBranchStart = IntProvider::uniform(lo, std::max(lo, start->maxValue() - 1));
        c->branchEnd = *end;
        p = std::move(c);
    } else {
        l.error("unknown trunk placer '" + type + "'");
        return nullptr;
    }
    p->baseHeight = l.getInt(j, "base_height", 0);
    p->randA = l.getInt(j, "height_rand_a", 0);
    p->randB = l.getInt(j, "height_rand_b", 0);
    if (p->baseHeight < 0 || p->randA < 0 || p->randB < 0) {
        l.error("trunk heights must not be negative");
        return nullptr;
    }
    return p;
}

std::unique_ptr<FoliagePlacer> parseFoliagePlacer(const Json& j, FeatureLoader& l) {
    const std::string type = treeTypeOf(j);
    auto intProvider = [&](const char* key, IntProvider& out) {
        if (!j.contains(key)) {
            l.error(std::string("foliage placer needs '") + key + "'");
            return false;
        }
        auto v = IntProvider::parse(j[key], l);
        if (v) out = *v;
        return v.has_value();
    };
    std::unique_ptr<FoliagePlacer> p;
    if (type == "blob_foliage_placer" || type == "bush_foliage_placer" || type == "fancy_foliage_placer") {
        std::unique_ptr<BlobFoliagePlacer> b;
        if (type == "blob_foliage_placer") b = std::make_unique<BlobFoliagePlacer>();
        else if (type == "bush_foliage_placer") b = std::make_unique<BushFoliagePlacer>();
        else b = std::make_unique<FancyFoliagePlacer>();
        b->height = l.getInt(j, "height", 3);
        p = std::move(b);
    } else if (type == "spruce_foliage_placer") {
        auto s = std::make_unique<SpruceFoliagePlacer>();
        if (!intProvider("trunk_height", s->trunkHeight)) return nullptr;
        p = std::move(s);
    } else if (type == "pine_foliage_placer") {
        auto s = std::make_unique<PineFoliagePlacer>();
        if (!intProvider("height", s->height)) return nullptr;
        p = std::move(s);
    } else if (type == "mega_pine_foliage_placer") {
        auto s = std::make_unique<MegaPineFoliagePlacer>();
        if (!intProvider("crown_height", s->crownHeight)) return nullptr;
        p = std::move(s);
    } else if (type == "acacia_foliage_placer") {
        p = std::make_unique<AcaciaFoliagePlacer>();
    } else if (type == "jungle_foliage_placer") {
        auto s = std::make_unique<JungleFoliagePlacer>();
        s->height = l.getInt(j, "height", 2);
        p = std::move(s);
    } else if (type == "dark_oak_foliage_placer") {
        p = std::make_unique<DarkOakFoliagePlacer>();
    } else if (type == "random_spread_foliage_placer") {
        auto s = std::make_unique<RandomSpreadFoliagePlacer>();
        if (!intProvider("foliage_height", s->height)) return nullptr;
        s->attempts = l.getInt(j, "leaf_placement_attempts", 128);
        p = std::move(s);
    } else if (type == "cherry_foliage_placer") {
        auto s = std::make_unique<CherryFoliagePlacer>();
        if (!intProvider("height", s->height)) return nullptr;
        s->wideBottomHole = l.getFloat(j, "wide_bottom_layer_hole_chance", 0.0f);
        s->cornerHole = l.getFloat(j, "corner_hole_chance", 0.0f);
        s->hanging = l.getFloat(j, "hanging_leaves_chance", 0.0f);
        s->hangingExtension = l.getFloat(j, "hanging_leaves_extension_chance", 0.0f);
        p = std::move(s);
    } else {
        l.error("unknown foliage placer '" + type + "'");
        return nullptr;
    }
    if (!intProvider("radius", p->radius) || !intProvider("offset", p->offset)) return nullptr;
    return p;
}

} // namespace mc
