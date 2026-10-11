#include <memory>

#include <gtest/gtest.h>

#include "resources/Atlas.h"
#include "resources/ItemDefinitions.h"
#include "resources/ItemModelGenerator.h"
#include "resources/ModelBaker.h"
#include "resources/ResourcePack.h"
#include "world/block/Tints.h"

using namespace mc;

namespace {

struct ItemModelFixture : ::testing::Test {
    static void SetUpTestSuite() {
        pack = std::make_unique<ResourcePack>(MC_DEFAULT_PACK_DIR);
        atlas = std::make_unique<Atlas>(*buildAtlas(*pack, "blocks", 0));
        store = std::make_unique<ModelStore>(*pack);
        baker = std::make_unique<ModelBaker>(*store, *atlas);
        defs = std::make_unique<ItemDefinitions>();
        defs->load(*pack, *baker, &problems);
    }
    static void TearDownTestSuite() {
        defs.reset();
        baker.reset();
        store.reset();
        atlas.reset();
        pack.reset();
    }
    static std::vector<ItemLayer> eval(const char* id, ItemModelContext ctx = {}) {
        std::vector<ItemLayer> layers;
        defs->evaluate(id, ctx, nullptr, layers);
        return layers;
    }
    static const BakedModel* model(const char* id) { return baker->bake(ModelVariant{id}); }

    static inline std::unique_ptr<ResourcePack> pack;
    static inline std::unique_ptr<Atlas> atlas;
    static inline std::unique_ptr<ModelStore> store;
    static inline std::unique_ptr<ModelBaker> baker;
    static inline std::unique_ptr<ItemDefinitions> defs;
    static inline std::string problems;
};

} // namespace

TEST_F(ItemModelFixture, EveryDefinitionLoads) {
    EXPECT_EQ(defs->size(), 1416u);
    EXPECT_EQ(problems, "");
    EXPECT_TRUE(baker->errors().empty()) << baker->errors().front();
    EXPECT_EQ(baker->missingTextureCount(), 0);
}

TEST_F(ItemModelFixture, BlockAndGeneratedModels) {
    auto stone = eval("minecraft:stone");
    ASSERT_EQ(stone.size(), 1u);
    EXPECT_EQ(stone[0].model, model("minecraft:block/stone"));
    EXPECT_EQ(stone[0].model->quads.size(), 6u);
    EXPECT_FALSE(stone[0].model->guiLightFront);

    // item/generated: the layer's front and back plus the edges of its opaque texels.
    auto diamond = eval("minecraft:diamond");
    ASSERT_EQ(diamond.size(), 1u);
    const BakedModel* m = diamond[0].model;
    EXPECT_TRUE(m->guiLightFront);
    int front = 0, back = 0, sides = 0;
    for (const BakedQuad& q : m->quads) {
        for (const glm::vec3& p : q.pos) {
            EXPECT_GE(p.z, 7.5f / 16.0f - 1e-5f);
            EXPECT_LE(p.z, 8.5f / 16.0f + 1e-5f);
        }
        if (q.face == Direction::South) ++front;
        else if (q.face == Direction::North) ++back;
        else ++sides;
    }
    EXPECT_EQ(front, 1);
    EXPECT_EQ(back, 1);
    EXPECT_GT(sides, 8);
    EXPECT_TRUE(m->hasDisplay[size_t(DisplayContext::FirstPersonRight)]);

    // Two layers: each keeps its own tint index.
    auto potion = eval("minecraft:potion");
    ASSERT_EQ(potion.size(), 1u);
    bool tint0 = false, untinted = false;
    for (const BakedQuad& q : potion[0].model->quads) (q.tint == 0 ? tint0 : untinted) = true;
    EXPECT_TRUE(tint0 && untinted);
    EXPECT_EQ(potion[0].tint(0), 0xFF000000u | (uint32_t(-13083194) & 0xFFFFFF));
}

// Edges facing each way, in the right places: a 4x4 opaque square in an 8x8 image.
TEST_F(ItemModelFixture, GeneratedSideFaces) {
    Image img(8, 8);
    for (int y = 2; y < 6; ++y)
        for (int x = 2; x < 6; ++x) img.px(x, y)[3] = 255;
    const auto elements = generateLayerElements(0, "layer0", {&img});
    int up = 0, down = 0, west = 0, east = 0;
    for (size_t i = 1; i < elements.size(); ++i) {
        const ElementDef& e = elements[i];
        if (e.faces[size_t(Direction::Up)]) {
            ++up;
            EXPECT_FLOAT_EQ(e.from.y, 16.0f - 2 * 2.0f);
        }
        if (e.faces[size_t(Direction::Down)]) {
            ++down;
            EXPECT_FLOAT_EQ(e.from.y, 16.0f - 6 * 2.0f);
        }
        if (e.faces[size_t(Direction::West)]) {
            ++west;
            EXPECT_FLOAT_EQ(e.from.x, 2 * 2.0f);
        }
        if (e.faces[size_t(Direction::East)]) {
            ++east;
            EXPECT_FLOAT_EQ(e.from.x, 6 * 2.0f);
        }
    }
    // Vanilla merges per row/column: every texel of the top row has an up edge, one span.
    EXPECT_EQ(up, 1);
    EXPECT_EQ(down, 1);
    EXPECT_EQ(west, 1);
    EXPECT_EQ(east, 1);
    EXPECT_EQ(elements.size(), 5u);
}

TEST_F(ItemModelFixture, SpecialModelsTintsAndProperties) {
    auto chest = eval("minecraft:chest");
    ASSERT_EQ(chest.size(), 1u);
    ASSERT_NE(chest[0].special, nullptr);
    EXPECT_EQ(chest[0].special->type, SpecialModel::Chest);
    EXPECT_TRUE(chest[0].special->texture == "minecraft:normal" || chest[0].special->texture == "minecraft:christmas");
    EXPECT_EQ(chest[0].transforms, model("minecraft:item/chest"));
    auto head = eval("minecraft:dragon_head");
    ASSERT_EQ(head.size(), 1u);
    EXPECT_EQ(head[0].special->kind, "dragon");

    // The trident is a flat icon in the GUI and the 3D model in hand.
    ItemModelContext hand;
    hand.display = DisplayContext::FirstPersonRight;
    EXPECT_EQ(eval("minecraft:trident")[0].model, model("minecraft:item/trident"));
    EXPECT_EQ(eval("minecraft:trident", hand)[0].special->type, SpecialModel::Trident);

    // Grass tint: the colormap at the definition's temperature and downfall.
    Colormap grass(*Image::loadPng(pack->file("minecraft", "textures/colormap/grass.png")));
    std::vector<ItemLayer> layers;
    defs->evaluate("minecraft:grass_block", {}, &grass, layers);
    ASSERT_EQ(layers.size(), 1u);
    EXPECT_EQ(layers[0].tint(0), 0xFF000000u | grass.sample(0.5, 1.0));
    EXPECT_EQ(layers[0].tint(1), 0xFFFFFFFFu); // no such index

    // Clocks follow the time of day: noon shows clock_00, midnight clock_32.
    ItemModelContext ctx;
    ctx.timeOfDay = 0.0f;
    EXPECT_EQ(eval("minecraft:clock", ctx)[0].model, model("minecraft:item/clock_00"));
    ctx.timeOfDay = 0.5f;
    EXPECT_EQ(eval("minecraft:clock", ctx)[0].model, model("minecraft:item/clock_32"));

    // Compasses point at the spawn: ahead (needle up, compass_16), or to the left (compass_08).
    ctx.holderPos = glm::dvec3(0.5, 64.0, 0.5);
    ctx.holderYaw = 0.0f; // facing south
    ctx.spawnPos = glm::dvec3(0.0, 64.0, 100.0);
    EXPECT_EQ(eval("minecraft:compass", ctx)[0].model, model("minecraft:item/compass_16"));
    ctx.spawnPos = glm::dvec3(100.0, 64.0, 0.0); // east, on the left when facing south
    EXPECT_EQ(eval("minecraft:compass", ctx)[0].model, model("minecraft:item/compass_08"));

    // Bundles in the GUI: the closed bundle while nothing is selected.
    EXPECT_EQ(eval("minecraft:bundle")[0].model, model("minecraft:item/bundle"));
    // Unknown items draw the missing model.
    EXPECT_EQ(eval("minecraft:no_such_item")[0].model, baker->missingModel());
}
