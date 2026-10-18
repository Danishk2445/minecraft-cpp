#include <chrono>
#include <memory>
#include <thread>

#include <unistd.h>

#include <gtest/gtest.h>

#include "core/JobSystem.h"
#include "game/Commands.h"
#include "resources/Atlas.h"
#include "resources/BlockModels.h"
#include "world/biome/Biome.h"
#include "world/block/BlockRegistry.h"
#include "world/chunk/ChunkManager.h"
#include "world/mesh/ChunkMesher.h"
#include "world/storage/WorldSave.h"

using namespace mc;

namespace {

struct WorldFixture : ::testing::Test {
    static void SetUpTestSuite() {
        pack = std::make_unique<ResourcePack>(MC_DEFAULT_PACK_DIR);
        atlas = std::make_unique<Atlas>(*buildAtlas(*pack, "blocks", 4));
        store = std::make_unique<ModelStore>(*pack);
        baker = std::make_unique<ModelBaker>(*store, *atlas);
        models = std::make_unique<BlockModels>();
        models->load(*pack, *store, *baker);
        registry = std::make_unique<BlockRegistry>();
        std::string err;
        registry->build(*models, *atlas, MC_DEFAULT_DATA_DIR, &err);
        biomes = std::make_unique<BiomeRegistry>();
        biomes->load(MC_DEFAULT_DATA_DIR, MC_DEFAULT_PACK_DIR, &err);
    }
    static void TearDownTestSuite() {
        biomes.reset();
        registry.reset();
        models.reset();
        baker.reset();
        store.reset();
        atlas.reset();
        pack.reset();
    }
    static StateId S(const char* text) { return *registry->parse(text); }

    // Runs the pipeline until the view is ready; returns a hash of every final chunk.
    static uint64_t generateAndHash(std::shared_ptr<const ChunkGenerator> gen, int threads, ChunkPos center, int rd) {
        JobSystem jobs(threads);
        ChunkManager cm(*registry, gen, jobs);
        cm.setView(center, rd);
        for (int i = 0; i < 1000 && !cm.viewReady(); ++i) {
            cm.update();
            jobs.waitIdle();
        }
        EXPECT_TRUE(cm.viewReady());
        uint64_t h = 1469598103934665603ull;
        for (int dz = -rd; dz <= rd; ++dz)
            for (int dx = -rd; dx <= rd; ++dx) {
                const Chunk* c = cm.chunk({center.x + dx, center.z + dz});
                EXPECT_NE(c, nullptr);
                if (!c) continue;
                for (int y = kWorldMinY; y <= kWorldMaxY; ++y)
                    for (int z = 0; z < 16; ++z)
                        for (int x = 0; x < 16; ++x) h = (h ^ c->get(x, y, z)) * 1099511628211ull;
            }
        return h;
    }

    // Meshes the section containing y=64 of a single chunk built by `fill`.
    template <class Fill>
    static SectionMesh meshOne(Fill fill, const MesherOptions& options = {}) {
        Chunk c({0, 0});
        fill(c);
        Neighborhood n;
        n.center = {0, 0};
        n.cols[4] = ColumnSnapshot::of(c);
        std::vector<SectionMesh> out;
        meshSections(*registry, options, n, 1u << sectionIndexOfY(64), out);
        return out.at(0);
    }
    static float vertexY(const TerrainVertex& v) { return float(v.y) / 2048.0f - 8.0f; }

    static inline std::unique_ptr<ResourcePack> pack;
    static inline std::unique_ptr<Atlas> atlas;
    static inline std::unique_ptr<ModelStore> store;
    static inline std::unique_ptr<ModelBaker> baker;
    static inline std::unique_ptr<BlockModels> models;
    static inline std::unique_ptr<BlockRegistry> registry;
    static inline std::unique_ptr<BiomeRegistry> biomes;
};

} // namespace

TEST_F(WorldFixture, FlatWorldLayers) {
    JobSystem jobs(4);
    ChunkManager cm(*registry, std::make_shared<FlatGenerator>(*registry), jobs);
    cm.setView({0, 0}, 2);
    for (int i = 0; i < 1000 && !cm.viewReady(); ++i) {
        cm.update();
        jobs.waitIdle();
    }
    ASSERT_TRUE(cm.viewReady());
    EXPECT_EQ(cm.getBlock({5, -64, 7}), S("bedrock"));
    EXPECT_EQ(cm.getBlock({5, -63, 7}), S("dirt"));
    EXPECT_EQ(cm.getBlock({5, -62, 7}), S("dirt"));
    EXPECT_EQ(registry->blockOf(cm.getBlock({5, -61, 7})).id, "minecraft:grass_block");
    EXPECT_EQ(cm.getBlock({5, -60, 7}), BlockRegistry::kAirState);
    // Edits work on ready chunks and are copy-on-write.
    EXPECT_TRUE(cm.setBlock({1, -60, 1}, S("stone")));
    EXPECT_EQ(cm.getBlock({1, -60, 1}), S("stone"));
}

TEST_F(WorldFixture, PipelineIsDeterministicAcrossThreadCounts) {
    auto flat = std::make_shared<FlatGenerator>(*registry);
    EXPECT_EQ(generateAndHash(flat, 0, {3, -2}, 3), generateAndHash(flat, 8, {3, -2}, 3));
    auto debug = std::make_shared<DebugGenerator>(*registry);
    const uint64_t a = generateAndHash(debug, 0, {6, 6}, 4);
    const uint64_t b = generateAndHash(debug, 8, {6, 6}, 4);
    const uint64_t c = generateAndHash(debug, 3, {6, 6}, 4);
    EXPECT_EQ(a, b);
    EXPECT_EQ(a, c);
}

TEST_F(WorldFixture, DebugWorldContainsEveryState) {
    DebugGenerator gen(*registry);
    std::vector<bool> seen(registry->stateCount(), false);
    const int span = gen.gridWidth() * 2 + 2;
    for (int x = 0; x <= span; ++x)
        for (int z = 0; z <= span; ++z) seen[gen.stateAt(x, z)] = true;
    for (size_t s = 1; s < registry->stateCount(); ++s)
        if (!(registry->info(StateId(s)).flags & kAir)) {
            EXPECT_TRUE(seen[s]) << registry->describe(StateId(s));
        }
}

TEST_F(WorldFixture, MesherCullsHiddenFaces) {
    auto quads = [](const SectionMesh& m) { return m.quadCount(); };
    const StateId stone = S("stone"), glass = S("glass");
    EXPECT_EQ(quads(meshOne([&](Chunk& c) { c.set(5, 64, 5, stone); })), 6u);
    EXPECT_EQ(quads(meshOne([&](Chunk& c) {
                  c.set(5, 64, 5, stone);
                  c.set(6, 64, 5, stone);
              })),
              10u);
    // A solid 3x3x3 cube shows only its 54 outer faces.
    EXPECT_EQ(quads(meshOne([&](Chunk& c) {
                  for (int x = 4; x < 7; ++x)
                      for (int y = 64; y < 67; ++y)
                          for (int z = 4; z < 7; ++z) c.set(x, y, z, stone);
              })),
              54u);
    // Glass never hides stone; stone hides the glass face touching it.
    EXPECT_EQ(quads(meshOne([&](Chunk& c) {
                  c.set(5, 64, 5, stone);
                  c.set(6, 64, 5, glass);
              })),
              11u);
    // Identical glass hides the shared faces (skip_same).
    EXPECT_EQ(quads(meshOne([&](Chunk& c) {
                  c.set(5, 64, 5, glass);
                  c.set(6, 64, 5, glass);
              })),
              10u);
    // Grass block: 6 faces + 4 side overlays.
    EXPECT_EQ(quads(meshOne([&](Chunk& c) { c.set(5, 64, 5, S("grass_block")); })), 10u);
}

TEST_F(WorldFixture, MesherPartialCoverage) {
    const StateId stone = S("stone"), slab = S("oak_slab[type=bottom]");
    // Slab west of stone: the slab's east side is covered by the stone, but the
    // stone's west face stays (the slab covers only its lower half).
    const SectionMesh m = meshOne([&](Chunk& c) {
        c.set(5, 64, 5, slab);
        c.set(6, 64, 5, stone);
    });
    // slab: 6 faces - 1 (east, covered) = 5; stone: 6 faces (west partially open) = 6.
    EXPECT_EQ(m.quadCount(), 11u);
    // Two bottom slabs side by side hide their touching half-faces.
    const SectionMesh pair = meshOne([&](Chunk& c) {
        c.set(5, 64, 5, slab);
        c.set(6, 64, 5, slab);
    });
    EXPECT_EQ(pair.quadCount(), 10u);
    // Layers: grass goes to cutout_mipped, glass to cutout, stained glass to translucent.
    const SectionMesh layers = meshOne([&](Chunk& c) {
        c.set(1, 64, 1, S("grass_block"));
        c.set(5, 64, 5, S("glass"));
        c.set(9, 64, 9, S("white_stained_glass"));
    });
    EXPECT_EQ(layers.layers[size_t(RenderLayer::Solid)].size(), 0u);
    EXPECT_EQ(layers.layers[size_t(RenderLayer::CutoutMipped)].size(), 40u);
    EXPECT_EQ(layers.layers[size_t(RenderLayer::Cutout)].size(), 24u);
    EXPECT_EQ(layers.layers[size_t(RenderLayer::Translucent)].size(), 24u);
    EXPECT_EQ(layers.translucentCenters.size(), 6u);
}

TEST_F(WorldFixture, FluidMeshing) {
    const StateId water = S("water"), stone = S("stone");
    const auto& translucent = [](const SectionMesh& m) -> const std::vector<TerrainVertex>& {
        return m.layers[size_t(RenderLayer::Translucent)];
    };
    // A lone source: top (+ its underside, open above), bottom and 4 double-sided sides.
    const SectionMesh lone = meshOne([&](Chunk& c) { c.set(8, 64, 8, water); });
    EXPECT_EQ(translucent(lone).size(), 11u * 4u);
    EXPECT_EQ(lone.translucentCenters.size(), 11u);
    // Corners average the source (8/9, weight 10) with two empty neighbors (weight 1 each).
    float top = 0.0f;
    for (const TerrainVertex& v : translucent(lone)) top = std::max(top, vertexY(v));
    EXPECT_NEAR(top, (8.0f / 9.0f * 10.0f) / 12.0f - 0.001f, 1e-3f);
    // Stone to the north hides that side (both of its faces); water above hides the top
    // and raises every corner to a full block.
    const SectionMesh walled = meshOne([&](Chunk& c) {
        c.set(8, 64, 8, water);
        c.set(8, 64, 7, stone);
    });
    EXPECT_EQ(translucent(walled).size(), 9u * 4u);
    const SectionMesh stacked = meshOne([&](Chunk& c) {
        c.set(8, 64, 8, water);
        c.set(8, 65, 8, water);
    });
    float lowerTop = 0.0f;
    for (const TerrainVertex& v : translucent(stacked))
        if (vertexY(v) < 1.5f) lowerTop = std::max(lowerTop, vertexY(v));
    EXPECT_NEAR(lowerTop, 1.0f, 1e-3f);
    // Lava is opaque: it goes to the solid layer and has no sort centers.
    const SectionMesh lava = meshOne([&](Chunk& c) { c.set(8, 64, 8, S("lava")); });
    EXPECT_GT(lava.layers[size_t(RenderLayer::Solid)].size(), 0u);
    EXPECT_TRUE(lava.translucentCenters.empty());
}

TEST_F(WorldFixture, TranslucentQuadsSortBackToFront) {
    SectionMesh m = meshOne([&](Chunk& c) {
        c.set(1, 64, 8, S("white_stained_glass"));
        c.set(14, 64, 8, S("white_stained_glass"));
    });
    auto& verts = m.layers[size_t(RenderLayer::Translucent)];
    ASSERT_EQ(m.translucentCenters.size() * 4, verts.size());
    for (const glm::vec3 eye : {glm::vec3(40.0f, 0.5f, 8.5f), glm::vec3(-40.0f, 0.5f, 8.5f)}) {
        sortTranslucentQuads(verts, m.translucentCenters, eye);
        for (size_t i = 1; i < m.translucentCenters.size(); ++i)
            EXPECT_GE(glm::distance(m.translucentCenters[i - 1], eye), glm::distance(m.translucentCenters[i], eye));
        // Vertices travel with their centers.
        for (size_t q = 0; q < m.translucentCenters.size(); ++q) {
            float x = 0;
            for (int k = 0; k < 4; ++k) x += float(verts[q * 4 + size_t(k)].x) / 2048.0f - 8.0f;
            EXPECT_NEAR(x / 4.0f, m.translucentCenters[q].x, 1e-3f);
        }
    }
}

TEST_F(WorldFixture, BiomeTintsBlend) {
    const BiomeId plains = biomes->require("plains"), jungle = biomes->require("jungle");
    MesherOptions opts;
    opts.biomes = biomes.get();
    opts.smoothLighting = false;
    auto grassTop = [&](int x, int z, auto setBiomes) {
        const SectionMesh m = meshOne(
            [&](Chunk& c) {
                setBiomes(c);
                c.set(x, 64, z, S("grass_block"));
            },
            opts);
        // The top face is the one whose four vertices sit at y = 1.
        for (const auto& layer : m.layers)
            for (size_t i = 0; i + 3 < layer.size(); i += 4)
                if (vertexY(layer[i]) > 0.99f && vertexY(layer[i + 2]) > 0.99f)
                    return (uint32_t(layer[i].r) << 16) | (uint32_t(layer[i].g) << 8) | layer[i].b;
        return 0u;
    };
    auto fill = [](BiomeId b) {
        return [b](Chunk& c) {
            for (auto& s : c.sections) s.biomes.fill(b);
        };
    };
    EXPECT_EQ(grassTop(8, 8, fill(plains)), biomes->colors(plains, 8, 8).grass);
    EXPECT_EQ(grassTop(8, 8, fill(jungle)), biomes->colors(jungle, 8, 8).grass);
    // Jungle in quart columns x >= 8: a block at x = 7 sees 2 jungle columns of 5.
    const uint32_t mixed = grassTop(7, 8, [&](Chunk& c) {
        for (auto& s : c.sections)
            for (int i = 0; i < 64; ++i) s.biomes[size_t(i)] = (i & 3) >= 2 ? jungle : plains;
    });
    const uint32_t p = biomes->colors(plains, 0, 0).grass, j = biomes->colors(jungle, 0, 0).grass;
    auto channel = [](uint32_t c, int shift) { return int((c >> shift) & 255); };
    for (int shift : {16, 8, 0})
        EXPECT_NEAR(channel(mixed, shift), (channel(p, shift) * 3 + channel(j, shift) * 2) / 5, 1);
}

TEST(Commands, Tokenize) {
    const auto t = tokenizeCommand("  /setblock ~ ~1 -3   oak_stairs[facing=east, half=top] ");
    ASSERT_EQ(t.size(), 5u);
    EXPECT_EQ(t[0], "/setblock");
    EXPECT_EQ(t[4], "oak_stairs[facing=east, half=top]");
    EXPECT_EQ(parseCoordinate("~", 5.5), 5.5);
    EXPECT_EQ(parseCoordinate("~-2", 5.5), 3.5);
    EXPECT_EQ(parseCoordinate("12.25", 5.5), 12.25);
    EXPECT_FALSE(parseCoordinate("abc", 0).has_value());
}

// Operator commands need "Allow Commands"; the basics answer like vanilla.
TEST(Commands, PermissionsAndBasics) {
    Commands commands;
    std::vector<std::string> out;
    Difficulty difficulty = Difficulty::Normal;
    CommandSource src;
    src.permission = 0;
    src.seed = -42;
    src.feedback = [&](const std::string& m) { out.push_back(m); };
    src.getDifficulty = [&] { return difficulty; };
    src.setDifficulty = [&](Difficulty d) { difficulty = d; };
    std::string err;
    // Without commands allowed, operator commands are unknown and /help leaves them out.
    EXPECT_FALSE(commands.execute(src, "/gamemode creative", &err));
    EXPECT_EQ(err, "Unknown or incomplete command 'gamemode'");
    EXPECT_TRUE(commands.execute(src, "/help", &err));
    EXPECT_EQ(out, (std::vector<std::string>{"/help [command]", "/list", "/me <action>", "/seed"}));
    out.clear();
    EXPECT_TRUE(commands.execute(src, "/seed"));
    EXPECT_TRUE(commands.execute(src, "/me waves hello"));
    EXPECT_EQ(out, (std::vector<std::string>{"Seed: [-42]", "* Player waves hello"}));
    out.clear();
    // With them: the difficulty, and its "did not change" error.
    src.permission = 4;
    EXPECT_TRUE(commands.execute(src, "/difficulty"));
    EXPECT_TRUE(commands.execute(src, "/difficulty hard"));
    EXPECT_EQ(difficulty, Difficulty::Hard);
    EXPECT_FALSE(commands.execute(src, "/difficulty hard", &err));
    EXPECT_EQ(out, (std::vector<std::string>{"The difficulty is Normal", "The difficulty has been set to Hard"}));
    EXPECT_GE(commands.names(4).size(), 15u);
}

// The world list and the Create World screen's folder names and seeds.
TEST(WorldSaves, FoldersSeedsAndListing) {
    const auto dir = std::filesystem::temp_directory_path() / ("mc_saves_test_" + std::to_string(::getpid()));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    EXPECT_EQ(uniqueWorldFolder(dir, "New World"), "New World");
    std::filesystem::create_directories(dir / "New World");
    EXPECT_EQ(uniqueWorldFolder(dir, "New World"), "New World (1)");
    std::filesystem::create_directories(dir / "New World (1)");
    EXPECT_EQ(uniqueWorldFolder(dir, "New World"), "New World (2)");
    EXPECT_EQ(uniqueWorldFolder(dir, "a/b:c*?"), "a_b_c__");
    EXPECT_EQ(uniqueWorldFolder(dir, "con"), "_con");
    EXPECT_EQ(uniqueWorldFolder(dir, "Wörld"), "Wörld");
    EXPECT_EQ(uniqueWorldFolder(dir, "  "), "World");

    // Seeds: numbers as typed, other text by Java's String.hashCode, empty is random.
    EXPECT_EQ(seedFromString("12345", 7), 12345);
    EXPECT_EQ(seedFromString("-9223372036854775808", 7), INT64_MIN);
    EXPECT_EQ(seedFromString("a", 7), 97);
    EXPECT_EQ(seedFromString("hello", 7), 99162322);
    EXPECT_EQ(seedFromString("Minecraft", 7), -1595926131);
    EXPECT_EQ(seedFromString("  ", 7), 7);

    // Listing: worlds with a level.json, most recently played first.
    WorldInfo a;
    a.name = "Alpha";
    ASSERT_TRUE(writeWorldInfo(dir / "New World", a));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    WorldInfo b;
    b.name = "Beta";
    b.gameMode = "creative";
    b.cheats = true;
    ASSERT_TRUE(writeWorldInfo(dir / "New World (1)", b));
    const auto worlds = listWorlds(dir);
    ASSERT_EQ(worlds.size(), 2u);
    EXPECT_EQ(worlds[0].info.name, "Beta");
    EXPECT_EQ(worlds[0].info.gameMode, "creative");
    EXPECT_TRUE(worlds[0].info.cheats);
    EXPECT_GT(worlds[0].info.lastPlayed, 0);
    EXPECT_EQ(worlds[1].folder, "New World");
    EXPECT_TRUE(deleteWorld(dir, "New World"));
    EXPECT_EQ(listWorlds(dir).size(), 1u);
    EXPECT_FALSE(deleteWorld(dir, ".."));
    std::filesystem::remove_all(dir);
}

// Cave culling (M13): the mesher's VisGraph of a stone section with a tunnel through it,
// and of sections that don't block sight.
TEST_F(WorldFixture, MesherResolvesSectionVisibility) {
    const StateId stone = S("stone"), glass = S("glass"), leaves = S("oak_leaves");
    // Stone with a 1x1 tunnel running north-south at x=8, y=72, a glass pane of it at z=8.
    const SectionMesh tunnel = meshOne([&](Chunk& c) {
        for (int y = 64; y < 80; ++y)
            for (int z = 0; z < 16; ++z)
                for (int x = 0; x < 16; ++x)
                    if (!(x == 8 && y == 72)) c.set(x, y, z, stone);
        c.set(8, 72, 8, glass);
    });
    EXPECT_TRUE(tunnel.visibility.visible(Direction::North, Direction::South));
    EXPECT_TRUE(tunnel.visibility.visible(Direction::South, Direction::North));
    EXPECT_FALSE(tunnel.visibility.visible(Direction::North, Direction::Up));
    EXPECT_FALSE(tunnel.visibility.visible(Direction::Down, Direction::Up));
    EXPECT_FALSE(tunnel.visibility.visible(Direction::West, Direction::East));
    // Solid stone: no face sees another. Leaves don't block sight.
    const SectionMesh solid = meshOne([&](Chunk& c) {
        for (int y = 64; y < 80; ++y)
            for (int z = 0; z < 16; ++z)
                for (int x = 0; x < 16; ++x) c.set(x, y, z, stone);
    });
    EXPECT_EQ(solid.visibility.bits, 0u);
    const SectionMesh leafy = meshOne([&](Chunk& c) {
        for (int y = 64; y < 80; ++y)
            for (int z = 0; z < 16; ++z)
                for (int x = 0; x < 16; ++x) c.set(x, y, z, leaves);
    });
    EXPECT_EQ(leafy.visibility.bits, VisibilitySet::kAll);
}

TEST_F(WorldFixture, MesherOffsetsPlants) {
    // A cross model centered in its cell: the mean of its corners is the cell's middle plus
    // the block's random offset.
    const auto center = [&](const char* id) {
        const SectionMesh m = meshOne([&](Chunk& c) { c.set(5, 64, 9, S(id)); });
        glm::dvec3 sum(0.0);
        size_t n = 0;
        for (const auto& layer : m.layers)
            for (const TerrainVertex& v : layer) {
                sum += glm::dvec3(v.x, v.y, v.z) / 2048.0 - 8.0;
                ++n;
            }
        EXPECT_GT(n, 0u) << id;
        return n ? sum / double(n) : sum;
    };
    for (const char* id : {"poppy", "short_grass"}) {
        const glm::dvec3 expected = glm::dvec3(5.5, 0.5, 9.5) + registry->modelOffset(S(id), BlockPos(5, 64, 9));
        const glm::dvec3 got = center(id);
        EXPECT_NEAR(got.x, expected.x, 1e-3) << id;
        EXPECT_NEAR(got.y, expected.y, 1e-3) << id;
        EXPECT_NEAR(got.z, expected.z, 1e-3) << id;
    }
    EXPECT_NE(registry->modelOffset(S("poppy"), BlockPos(5, 64, 9)), glm::dvec3(0.0));
}
