// Cave culling (M13): vanilla VisGraph and the SectionOcclusionGraph search over synthetic
// columns of section visibility.
#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <tuple>

#include <gtest/gtest.h>

#include "world/mesh/VisGraph.h"

using namespace mc;

namespace {

int cell(int x, int y, int z) { return x | z << 4 | y << 8; }

// A world of columns around the origin, every section `fill` unless overridden.
struct Columns {
    std::map<std::pair<int, int>, std::unique_ptr<ColumnVisibility>> columns;
    Columns(int radius, VisibilitySet fill) {
        for (int z = -radius; z <= radius; ++z)
            for (int x = -radius; x <= radius; ++x) {
                auto c = std::make_unique<ColumnVisibility>();
                c->visibility.fill(fill);
                columns[{x, z}] = std::move(c);
            }
    }
    VisibilitySet& at(int x, int z, int si) { return columns.at({x, z})->visibility[size_t(si)]; }
    std::vector<std::pair<ChunkPos, const ColumnVisibility*>> list() const {
        std::vector<std::pair<ChunkPos, const ColumnVisibility*>> out;
        for (const auto& [p, c] : columns) out.emplace_back(ChunkPos(p.first, p.second), c.get());
        return out;
    }
};

using Key = std::tuple<int, int, int>; // chunk x, chunk z, section
std::set<Key> reachedSet(const SectionOcclusionGraph& g) {
    std::set<Key> s;
    for (const auto& r : g.reached()) s.insert({r.chunk.x, r.chunk.z, r.index});
    return s;
}

// Columns whose centers are within the renderer's cylinder.
int columnsInView(int renderDistance, const glm::dvec3& camera) {
    const double maxDist = renderDistance * 16.0 + 16.0;
    const ChunkPos cc = ChunkPos::of(BlockPos::containing(camera));
    int n = 0;
    for (int dz = -renderDistance - 1; dz <= renderDistance + 1; ++dz)
        for (int dx = -renderDistance - 1; dx <= renderDistance + 1; ++dx) {
            const double x = (cc.x + dx) * 16.0 + 8.0 - camera.x, z = (cc.z + dz) * 16.0 + 8.0 - camera.z;
            n += x * x + z * z <= maxDist * maxDist;
        }
    return n;
}

} // namespace

TEST(VisGraph, ResolvesWhichFacesSeeEachOther) {
    std::bitset<4096> opaque;
    EXPECT_EQ(resolveVisibility(opaque).bits, VisibilitySet::kAll);
    for (int i = 0; i < 255; ++i) opaque.set(size_t(i * 16));
    EXPECT_EQ(resolveVisibility(opaque).bits, VisibilitySet::kAll); // fewer than 256 opaque: all
    opaque.set();
    EXPECT_EQ(resolveVisibility(opaque).bits, 0u);

    // A floor across y=8 (exactly 256 cells): above and below see every side, not each other.
    opaque.reset();
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) opaque.set(size_t(cell(x, 8, z)));
    const VisibilitySet floor = resolveVisibility(opaque);
    EXPECT_FALSE(floor.visible(Direction::Up, Direction::Down));
    EXPECT_TRUE(floor.visible(Direction::Up, Direction::North));
    EXPECT_TRUE(floor.visible(Direction::Down, Direction::East));
    EXPECT_TRUE(floor.visible(Direction::North, Direction::South)); // both halves touch both
    for (Direction d : kDirections) EXPECT_TRUE(floor.visible(d, d));

    // Solid but for an L-shaped tunnel from the west face to the top face.
    opaque.set();
    for (int x = 0; x <= 5; ++x) opaque.reset(size_t(cell(x, 5, 5)));
    for (int y = 5; y < 16; ++y) opaque.reset(size_t(cell(5, y, 5)));
    const VisibilitySet bend = resolveVisibility(opaque);
    EXPECT_TRUE(bend.visible(Direction::West, Direction::Up));
    EXPECT_TRUE(bend.visible(Direction::Up, Direction::West));
    EXPECT_FALSE(bend.visible(Direction::West, Direction::East));
    EXPECT_FALSE(bend.visible(Direction::Up, Direction::Down));
    EXPECT_FALSE(bend.visible(Direction::North, Direction::North)); // no open cell on it
}

TEST(SectionOcclusion, OpenWorldReachesEverySectionInView) {
    constexpr int rd = 6;
    Columns world(rd + 1, VisibilitySet{});
    SectionOcclusionGraph g;
    for (const glm::dvec3 camera : {glm::dvec3(8, 70, 8), glm::dvec3(-3.5, -50, 12), glm::dvec3(5, 400, 5),
                                    glm::dvec3(5, -100, 5)}) {
        g.search(camera, rd, world.list());
        EXPECT_EQ(int(reachedSet(g).size()), columnsInView(rd, camera) * kSectionCount) << camera.y;
        EXPECT_EQ(g.reached().size(), reachedSet(g).size()); // each section once
    }
}

TEST(SectionOcclusion, ClosedRoomSeesOnlyItsNeighbors) {
    constexpr int rd = 6;
    Columns world(rd + 1, VisibilitySet::none());
    SectionOcclusionGraph g;
    const glm::dvec3 camera(8, 70, 8); // section 8 of column 0,0
    g.search(camera, rd, world.list());
    const std::set<Key> expect = {{0, 0, 8}, {0, 0, 7}, {0, 0, 9}, {-1, 0, 8}, {1, 0, 8}, {0, -1, 8}, {0, 1, 8}};
    EXPECT_EQ(reachedSet(g), expect);
}

TEST(SectionOcclusion, TunnelsLeadOnlyWhereTheirFacesSee) {
    constexpr int rd = 8;
    Columns world(rd + 1, VisibilitySet::none());
    // A west-east tunnel at section 8 from the camera's column to x=2, a bend up at x=3
    // (west sees up), and an open shaft above it.
    VisibilitySet westEast = VisibilitySet::none();
    westEast.set(Direction::West, Direction::East);
    VisibilitySet westUp = VisibilitySet::none();
    westUp.set(Direction::West, Direction::Up);
    for (int x = 1; x <= 2; ++x) world.at(x, 0, 8) = westEast;
    world.at(3, 0, 8) = westUp;
    for (int si = 9; si < kSectionCount; ++si) world.at(3, 0, si) = VisibilitySet{};
    SectionOcclusionGraph g;
    g.search(glm::dvec3(8, 70, 8), rd, world.list());
    const std::set<Key> got = reachedSet(g);
    for (int x = 1; x <= 3; ++x) EXPECT_TRUE(got.contains({x, 0, 8})) << x;
    EXPECT_FALSE(got.contains({4, 0, 8})); // the bend doesn't see east
    EXPECT_FALSE(got.contains({1, 1, 8})); // the tunnel's walls: entered from the west, it only leads east
    EXPECT_FALSE(got.contains({2, 0, 9}));
    // Up the shaft, and out of it east or sideways, but never back west.
    for (int si = 9; si <= 11; ++si) EXPECT_TRUE(got.contains({3, 0, si})) << si;
    EXPECT_TRUE(got.contains({4, 0, 10}));
    EXPECT_TRUE(got.contains({3, 1, 10}));
    EXPECT_FALSE(got.contains({2, 0, 10}));
    // More than 60 blocks up, the shaft is out of the line of sight (through solid sections).
    EXPECT_FALSE(got.contains({3, 0, 14}));
}

TEST(SectionOcclusion, FarSectionsNeedALineOfSight) {
    constexpr int rd = 10;
    // Open everywhere but a solid wall three sections thick across x=5..7, with a tunnel
    // through it at section 8, z=0. The wall's near face is reached (entered, though not
    // seen through), so the line toward the camera from what lies beyond only passes when
    // it runs through the tunnel.
    Columns world(rd + 1, VisibilitySet{});
    VisibilitySet westEast = VisibilitySet::none();
    westEast.set(Direction::West, Direction::East);
    for (int x = 5; x <= 7; ++x)
        for (int z = -rd - 1; z <= rd + 1; ++z)
            for (int si = 0; si < kSectionCount; ++si)
                world.at(x, z, si) = z == 0 && si == 8 ? westEast : VisibilitySet::none();
    SectionOcclusionGraph g;
    g.search(glm::dvec3(8, 70, 8), rd, world.list());
    const std::set<Key> got = reachedSet(g);
    EXPECT_TRUE(got.contains({5, 3, 8}));   // the wall's near face
    EXPECT_FALSE(got.contains({6, 3, 8}));  // inside it
    for (int x = 5; x <= 10; ++x) EXPECT_TRUE(got.contains({x, 0, 8})) << x; // through the tunnel
    EXPECT_FALSE(got.contains({9, 5, 8}));  // beyond the wall off to the side
    EXPECT_FALSE(got.contains({9, 0, 20})); // or high above
}

TEST(SectionOcclusion, UnmeshedColumnsAreNotSearchedThrough) {
    constexpr int rd = 4;
    Columns world(rd + 1, VisibilitySet{});
    world.columns.erase({1, 0}); // not meshed yet
    SectionOcclusionGraph g;
    const glm::dvec3 camera(8, 70, 8);
    g.search(camera, rd, world.list());
    const std::set<Key> got = reachedSet(g);
    EXPECT_FALSE(got.contains({1, 0, 8}));
    // Paths never turn back along an axis, so the columns straight behind it (same z) wait
    // for it too; the rest are reached.
    EXPECT_FALSE(got.contains({2, 0, 8}));
    EXPECT_TRUE(got.contains({2, 1, 8}));
    const double maxDist = rd * 16.0 + 16.0;
    int behind = 0;
    for (int x = 2; x <= rd + 1; ++x) behind += double(x * 16) * double(x * 16) <= maxDist * maxDist;
    EXPECT_EQ(int(got.size()), (columnsInView(rd, camera) - 1 - behind) * kSectionCount);
}
