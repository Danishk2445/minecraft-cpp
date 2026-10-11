#include <memory>

#include <gtest/gtest.h>

#include "resources/Atlas.h"
#include "resources/BlockModels.h"
#include "world/block/BlockRegistry.h"

using namespace mc;

namespace {

struct RegistryFixture : ::testing::Test {
    static void SetUpTestSuite() {
        pack = std::make_unique<ResourcePack>(MC_DEFAULT_PACK_DIR);
        atlas = std::make_unique<Atlas>(*buildAtlas(*pack, "blocks", 4));
        store = std::make_unique<ModelStore>(*pack);
        baker = std::make_unique<ModelBaker>(*store, *atlas);
        models = std::make_unique<BlockModels>();
        models->load(*pack, *store, *baker);
        registry = std::make_unique<BlockRegistry>();
        ok = registry->build(*models, *atlas, MC_DEFAULT_DATA_DIR, &error);
    }
    static void TearDownTestSuite() {
        registry.reset();
        models.reset();
        baker.reset();
        store.reset();
        atlas.reset();
        pack.reset();
    }
    static StateId state(const std::string& text) {
        std::string err;
        auto s = registry->parse(text, &err);
        EXPECT_TRUE(s.has_value()) << text << ": " << err;
        return s.value_or(BlockRegistry::kAirState);
    }
    static inline std::unique_ptr<ResourcePack> pack;
    static inline std::unique_ptr<Atlas> atlas;
    static inline std::unique_ptr<ModelStore> store;
    static inline std::unique_ptr<ModelBaker> baker;
    static inline std::unique_ptr<BlockModels> models;
    static inline std::unique_ptr<BlockRegistry> registry;
    static inline bool ok = false;
    static inline std::string error;
};

} // namespace

TEST_F(RegistryFixture, BuildsWithoutProblems) {
    EXPECT_TRUE(ok) << error;
    EXPECT_EQ(registry->blockCount(), 1107u);
    EXPECT_LT(registry->stateCount(), 65535u);
    EXPECT_GT(registry->stateCount(), 20000u);
    EXPECT_EQ(registry->blockOf(BlockRegistry::kAirState).id, "minecraft:air");
    EXPECT_TRUE(registry->info(BlockRegistry::kAirState).flags & kAir);
}

TEST_F(RegistryFixture, EveryPackPropertyValueIsInTheSchema) {
    for (const auto& def : models->definitions()) {
        const Block* b = registry->find(def.block);
        ASSERT_NE(b, nullptr) << def.block;
        for (const auto& [name, values] : def.mentionedProperties()) {
            const Property* prop = nullptr;
            for (uint16_t id : b->props)
                if (registry->property(id).name == name) prop = &registry->property(id);
            ASSERT_NE(prop, nullptr) << def.block << " lacks property " << name;
            for (const auto& v : values) EXPECT_GE(prop->index(v), 0) << def.block << " " << name << "=" << v;
        }
    }
}

TEST_F(RegistryFixture, EveryStateRoundTripsAndHasModels) {
    for (size_t s = 0; s < registry->stateCount(); ++s) {
        const StateId id = StateId(s);
        const std::string text = registry->describe(id);
        auto back = registry->parse(text);
        ASSERT_TRUE(back.has_value()) << text;
        ASSERT_EQ(*back, id) << text;
        const Block& b = registry->blockOf(id);
        const BlockStateDef* def = models->find(b.id);
        ASSERT_NE(def, nullptr);
        if (!def->multipart) {
            EXPECT_EQ(registry->modelChoices(id).size(), 1u) << text;
        }
    }
}

TEST_F(RegistryFixture, DefaultsAndWith) {
    const StateId stairs = registry->defaultState("oak_stairs");
    EXPECT_EQ(registry->describe(stairs),
              "minecraft:oak_stairs[facing=north,half=bottom,shape=straight,waterlogged=false]");
    auto east = registry->with(stairs, "facing", "east");
    ASSERT_TRUE(east);
    EXPECT_EQ(registry->value(*east, "facing"), "east");
    EXPECT_EQ(registry->value(*east, "half"), "bottom");
    EXPECT_FALSE(registry->with(stairs, "facing", "up").has_value());
    EXPECT_EQ(registry->describe(registry->defaultState("cobblestone_wall")),
              "minecraft:cobblestone_wall[east=none,north=none,south=none,up=true,waterlogged=false,west=none]");
    EXPECT_EQ(registry->intValue(registry->defaultState("oak_leaves"), "distance"), 7);
}

TEST_F(RegistryFixture, RenderAndLightData) {
    const StateInfo& stone = registry->info(state("stone"));
    EXPECT_TRUE(stone.flags & kOpaqueCube);
    EXPECT_EQ(stone.opacity, 15);
    EXPECT_EQ(stone.layer, RenderLayer::Solid);

    const StateInfo& grass = registry->info(state("grass_block"));
    EXPECT_TRUE(grass.flags & kOpaqueCube) << "the overlay must not stop grass from occluding";
    EXPECT_EQ(grass.layer, RenderLayer::CutoutMipped);

    const StateInfo& glass = registry->info(state("glass"));
    EXPECT_FALSE(glass.flags & kOpaqueCube);
    EXPECT_EQ(glass.opacity, 0);
    EXPECT_EQ(glass.layer, RenderLayer::Cutout);

    const StateInfo& leaves = registry->info(state("oak_leaves"));
    EXPECT_FALSE(leaves.flags & kOpaqueCube);
    EXPECT_EQ(leaves.opacity, 1);
    EXPECT_EQ(leaves.layer, RenderLayer::CutoutMipped);

    EXPECT_EQ(registry->info(state("white_stained_glass")).layer, RenderLayer::Translucent);
    EXPECT_EQ(registry->info(state("ice")).opacity, 1);

    const StateInfo& water = registry->info(state("water[level=0]"));
    EXPECT_EQ(water.fluid.kind, FluidKind::Water);
    EXPECT_TRUE(water.fluid.source());
    EXPECT_EQ(water.layer, RenderLayer::Translucent);
    const StateInfo& falling = registry->info(state("water[level=9]"));
    EXPECT_TRUE(falling.fluid.falling);
    EXPECT_EQ(falling.fluid.level, 1);
    EXPECT_EQ(registry->info(state("oak_stairs[waterlogged=true]")).fluid.kind, FluidKind::Water);
    EXPECT_EQ(registry->info(state("seagrass")).fluid.kind, FluidKind::Water);
}

TEST_F(RegistryFixture, SlabCoverage) {
    const StateInfo& bottom = registry->info(state("oak_slab[type=bottom]"));
    EXPECT_FALSE(bottom.flags & kOpaqueCube);
    EXPECT_EQ(bottom.coverage[size_t(Direction::Down)], 0xFFFF);
    EXPECT_EQ(bottom.coverage[size_t(Direction::Up)], 0);
    EXPECT_EQ(bottom.coverage[size_t(Direction::North)], 0x00FF); // lower two rows
    EXPECT_TRUE(bottom.lightBlockingFaces & (1u << int(Direction::Down)));
    EXPECT_FALSE(bottom.lightBlockingFaces & (1u << int(Direction::Up)));
    const StateInfo& dbl = registry->info(state("oak_slab[type=double]"));
    EXPECT_TRUE(dbl.flags & kOpaqueCube);
}

TEST_F(RegistryFixture, EmissionExpressions) {
    EXPECT_EQ(registry->info(state("torch")).emission, 14);
    EXPECT_EQ(registry->info(state("glowstone")).emission, 15);
    EXPECT_EQ(registry->info(state("furnace[lit=true]")).emission, 13);
    EXPECT_EQ(registry->info(state("furnace[lit=false]")).emission, 0);
    EXPECT_EQ(registry->info(state("sea_pickle[pickles=4,waterlogged=true]")).emission, 15);
    EXPECT_EQ(registry->info(state("sea_pickle[pickles=1,waterlogged=false]")).emission, 0);
    EXPECT_EQ(registry->info(state("respawn_anchor[charges=2]")).emission, 7);
    EXPECT_EQ(registry->info(state("candle[candles=3,lit=true]")).emission, 9);
    EXPECT_EQ(registry->info(state("light[level=5]")).emission, 5);
    EXPECT_EQ(registry->info(state("redstone_torch")).emission, 7); // lit by default
}

TEST(Glob, Matches) {
    EXPECT_TRUE(globMatch("*_stairs", "oak_stairs"));
    EXPECT_FALSE(globMatch("*_stairs", "oak_stairs_x"));
    EXPECT_TRUE(globMatch("*candle", "candle"));
    EXPECT_TRUE(globMatch("*candle", "white_candle"));
    EXPECT_FALSE(globMatch("*candle", "candle_cake"));
    EXPECT_TRUE(globMatch("a*b*c", "aXXbYYc"));
    EXPECT_TRUE(globMatch("stone", "stone"));
    EXPECT_FALSE(globMatch("stone", "stones"));
}
