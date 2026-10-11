#include <algorithm>
#include <memory>

#include <gtest/gtest.h>

#include "resources/Atlas.h"
#include "resources/BlockModels.h"
#include "resources/ModelBaker.h"
#include "world/block/Tints.h"

using namespace mc;

namespace {

const std::filesystem::path kPackRoot = MC_DEFAULT_PACK_DIR;

struct PackFixture : ::testing::Test {
    static void SetUpTestSuite() {
        pack = std::make_unique<ResourcePack>(kPackRoot);
        AtlasStats stats;
        atlas = std::make_unique<Atlas>(*buildAtlas(*pack, "blocks", 4, &stats));
        atlasErrors = stats.errors;
        store = std::make_unique<ModelStore>(*pack);
        baker = std::make_unique<ModelBaker>(*store, *atlas);
        blocks = std::make_unique<BlockModels>();
        blocks->load(*pack, *store, *baker);
    }
    static void TearDownTestSuite() {
        blocks.reset();
        baker.reset();
        store.reset();
        atlas.reset();
        pack.reset();
    }
    static inline std::unique_ptr<ResourcePack> pack;
    static inline std::unique_ptr<Atlas> atlas;
    static inline std::vector<std::string> atlasErrors;
    static inline std::unique_ptr<ModelStore> store;
    static inline std::unique_ptr<ModelBaker> baker;
    static inline std::unique_ptr<BlockModels> blocks;
};

std::string joinFirst(const std::vector<std::string>& v, size_t n = 10) {
    std::string s;
    for (size_t i = 0; i < std::min(n, v.size()); ++i) s += "\n  " + v[i];
    return s;
}

// A sprite whose atlas UVs equal its pixel coordinates, to test UV math directly.
SpriteInfo pixelSprite() {
    SpriteInfo s;
    s.width = s.height = 16;
    s.u0 = s.v0 = 0;
    s.u1 = s.v1 = 16;
    return s;
}

glm::vec4 uvBounds(const BakedQuad& q) {
    glm::vec2 lo(1e9f), hi(-1e9f);
    for (const auto& uv : q.uv) {
        lo = glm::min(lo, uv);
        hi = glm::max(hi, uv);
    }
    return {lo.x, lo.y, hi.x, hi.y};
}

} // namespace

TEST_F(PackFixture, AtlasStitchesWithoutErrors) {
    EXPECT_TRUE(atlasErrors.empty()) << joinFirst(atlasErrors);
    EXPECT_GT(atlas->sprites.size(), 1800u);
    EXPECT_GE(atlas->animations.size(), 50u);
    EXPECT_TRUE(atlas->has("minecraft:block/stone"));
    EXPECT_TRUE(atlas->has("minecraft:item/diamond_pickaxe"));
    EXPECT_TRUE(atlas->has("minecraft:entity/bell/bell_body"));
    // Sprites never overlap.
    for (size_t i = 0; i < atlas->sprites.size(); ++i)
        for (size_t j = i + 1; j < atlas->sprites.size(); ++j) {
            const auto& a = atlas->sprites[i];
            const auto& b = atlas->sprites[j];
            const bool overlap = a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height &&
                                 b.y < a.y + a.height;
            ASSERT_FALSE(overlap) << a.name << " overlaps " << b.name;
        }
    // Mip alignment: every sprite starts on a 16-texel boundary.
    for (const auto& s : atlas->sprites) {
        EXPECT_EQ(s.x % 16, 0) << s.name;
        EXPECT_EQ(s.y % 16, 0) << s.name;
    }
    const auto& leaves = atlas->sprite("minecraft:block/oak_leaves");
    EXPECT_TRUE(leaves.transparent);
    EXPECT_FALSE(atlas->sprite("minecraft:block/stone").transparent);
    EXPECT_TRUE(atlas->sprite("minecraft:block/white_stained_glass").translucent);
}

TEST_F(PackFixture, EveryBlockstateParsesAndBakes) {
    EXPECT_EQ(blocks->definitions().size(), 1107u);
    EXPECT_TRUE(blocks->errors().empty()) << joinFirst(blocks->errors());
    EXPECT_TRUE(store->errors().empty()) << joinFirst(store->errors());
    EXPECT_TRUE(baker->errors().empty()) << joinFirst(baker->errors());
    EXPECT_EQ(baker->missingTextureCount(), 0);
}

TEST_F(PackFixture, EveryBlockModelFileResolves) {
    ModelStore fresh(*pack);
    const auto names = pack->list("minecraft", "models/block", ".json");
    EXPECT_EQ(names.size(), 2242u);
    int withElements = 0;
    for (const auto& n : names) {
        auto r = fresh.resolve("minecraft:block/" + n);
        ASSERT_TRUE(r.has_value()) << n << joinFirst(fresh.errors());
        // Abstract templates (template_*, *_inventory parents) leave variables unbound on
        // purpose; texture resolution of every model a blockstate uses is checked by
        // EveryBlockstateParsesAndBakes.
        if (r->elements) ++withElements;
    }
    EXPECT_GT(withElements, 2000);
    EXPECT_TRUE(fresh.errors().empty()) << joinFirst(fresh.errors());
}

TEST_F(PackFixture, StoneHasFourRandomVariantsAndGrassAnOverlay) {
    const BlockStateDef* stone = blocks->find("minecraft:stone");
    ASSERT_NE(stone, nullptr);
    ASSERT_EQ(stone->variants.size(), 1u);
    EXPECT_EQ(stone->variants[0].choice.options.size(), 4u);

    const BlockStateDef* grass = blocks->find("minecraft:grass_block");
    ASSERT_NE(grass, nullptr);
    const auto choices = blocks->choicesFor(*grass, [](std::string_view p) -> std::string_view {
        return p == "snowy" ? "false" : "";
    });
    ASSERT_EQ(choices.size(), 1u);
    const BakedModel* m = choices[0]->models[0];
    EXPECT_EQ(m->quads.size(), 10u); // 6 base faces + 4 side overlays
    int tinted = 0;
    for (const auto& q : m->quads) tinted += q.tint >= 0;
    EXPECT_EQ(tinted, 5); // top + 4 overlays
}

TEST_F(PackFixture, MultipartFenceSelection) {
    const BlockStateDef* fence = blocks->find("minecraft:oak_fence");
    ASSERT_NE(fence, nullptr);
    ASSERT_TRUE(fence->multipart);
    auto onlyNorth = [](std::string_view p) -> std::string_view { return p == "north" ? "true" : "false"; };
    EXPECT_EQ(blocks->choicesFor(*fence, onlyNorth).size(), 2u); // post + north arm
    auto all = [](std::string_view) -> std::string_view { return "true"; };
    EXPECT_EQ(blocks->choicesFor(*fence, all).size(), 5u);
}

TEST_F(PackFixture, MentionedPropertiesAreCompleted) {
    const BlockStateDef* wall = blocks->find("minecraft:cobblestone_wall");
    ASSERT_NE(wall, nullptr);
    const auto props = wall->mentionedProperties();
    auto values = [&](const std::string& name) {
        auto it = std::find_if(props.begin(), props.end(), [&](const auto& p) { return p.first == name; });
        auto v = it == props.end() ? std::vector<std::string>{} : it->second;
        std::sort(v.begin(), v.end());
        return v;
    };
    EXPECT_EQ(values("north"), (std::vector<std::string>{"low", "none", "tall"}));
    EXPECT_EQ(values("up"), (std::vector<std::string>{"false", "true"}));

    const BlockStateDef* wire = blocks->find("minecraft:redstone_wire");
    ASSERT_NE(wire, nullptr);
    auto wp = wire->mentionedProperties();
    auto it = std::find_if(wp.begin(), wp.end(), [](const auto& p) { return p.first == "east"; });
    ASSERT_NE(it, wp.end());
    EXPECT_EQ(it->second.size(), 3u); // none, side, up
}

TEST(Baking, BlockStateRotation) {
    const glm::mat3 y90 = blockStateRotation(0, 90);
    EXPECT_EQ(nearestDirection(y90 * glm::vec3(normal(Direction::North))), Direction::East);
    EXPECT_EQ(nearestDirection(y90 * glm::vec3(normal(Direction::East))), Direction::South);
    EXPECT_EQ(nearestDirection(y90 * glm::vec3(normal(Direction::Up))), Direction::Up);
    const glm::mat3 x90 = blockStateRotation(90, 0);
    EXPECT_EQ(nearestDirection(x90 * glm::vec3(normal(Direction::Up))), Direction::North);
    EXPECT_EQ(nearestDirection(x90 * glm::vec3(normal(Direction::North))), Direction::Down);
    // Vanilla applies x first, then y: up --x90--> north --y90--> east.
    const glm::mat3 both = blockStateRotation(90, 90);
    EXPECT_EQ(nearestDirection(both * glm::vec3(normal(Direction::Up))), Direction::East);
}

TEST(Baking, AutoUVMatchesVanilla) {
    const glm::vec3 from(2, 3, 4), to(10, 12, 14);
    EXPECT_EQ(autoFaceUV(Direction::Down, from, to), (std::array<float, 4>{2, 2, 10, 12}));
    EXPECT_EQ(autoFaceUV(Direction::Up, from, to), (std::array<float, 4>{2, 4, 10, 14}));
    EXPECT_EQ(autoFaceUV(Direction::North, from, to), (std::array<float, 4>{6, 4, 14, 13}));
    EXPECT_EQ(autoFaceUV(Direction::South, from, to), (std::array<float, 4>{2, 4, 10, 13}));
    EXPECT_EQ(autoFaceUV(Direction::West, from, to), (std::array<float, 4>{4, 4, 14, 13}));
    EXPECT_EQ(autoFaceUV(Direction::East, from, to), (std::array<float, 4>{2, 4, 12, 13}));
}

TEST(Baking, FaceRotatesWithBlockstate) {
    ElementDef cube;
    FaceDef f;
    f.cull = int(Direction::North);
    BakedQuad q;
    ASSERT_TRUE(bakeFace(cube, Direction::North, f, pixelSprite(), blockStateRotation(0, 90), false, q));
    EXPECT_EQ(q.face, Direction::East);
    EXPECT_EQ(q.cull, int8_t(Direction::East));
    for (const auto& p : q.pos) EXPECT_NEAR(p.x, 1.0f, 1e-5f);
}

TEST(Baking, RotatedFacesUseCanonicalWinding) {
    // A north face rotated to east must list its corners in the vanilla EAST order:
    // (X1,Y1,Z1) (X1,Y0,Z1) (X1,Y0,Z0) (X1,Y1,Z0).
    ElementDef cube;
    FaceDef f;
    BakedQuad q;
    ASSERT_TRUE(bakeFace(cube, Direction::North, f, pixelSprite(), blockStateRotation(0, 90), false, q));
    ASSERT_EQ(q.face, Direction::East);
    const glm::vec3 want[4] = {{1, 1, 1}, {1, 0, 1}, {1, 0, 0}, {1, 1, 0}};
    for (int i = 0; i < 4; ++i) {
        EXPECT_NEAR(glm::distance(q.pos[size_t(i)], want[i]), 0.0f, 1e-5f) << i;
    }
    // The original north vertex 0 (X1,Y1,Z0) lands on (X1,Y1,Z1) after y90 and keeps uv (0,0).
    EXPECT_NEAR(q.uv[0].x, 0.016f, 0.02f);
    EXPECT_NEAR(q.uv[0].y, 0.016f, 0.02f);
}

TEST(Baking, UvlockKeepsStairStepWorldAligned) {
    // The upper step of block/stairs: its top face, rotated y=90 with uvlock.
    ElementDef step;
    step.from = {8, 8, 0};
    step.to = {16, 16, 16};
    FaceDef up;
    up.uv = {8, 0, 16, 16};
    up.hasUv = true;
    BakedQuad q;
    ASSERT_TRUE(bakeFace(step, Direction::Up, up, pixelSprite(), blockStateRotation(0, 90), true, q));
    const glm::vec4 b = uvBounds(q);
    EXPECT_NEAR(b.x, 0.0f, 0.02f);
    EXPECT_NEAR(b.y, 8.0f, 0.02f);
    EXPECT_NEAR(b.z, 16.0f, 0.02f);
    EXPECT_NEAR(b.w, 16.0f, 0.02f);
    // Every vertex samples the world-projected texel: u = x, v = z (up face).
    for (int i = 0; i < 4; ++i) {
        EXPECT_NEAR(q.uv[size_t(i)].x, q.pos[size_t(i)].x * 16.0f, 0.02f);
        EXPECT_NEAR(q.uv[size_t(i)].y, q.pos[size_t(i)].z * 16.0f, 0.02f);
    }
    // Without uvlock the texture turns with the block.
    BakedQuad r;
    ASSERT_TRUE(bakeFace(step, Direction::Up, up, pixelSprite(), blockStateRotation(0, 90), false, r));
    EXPECT_NEAR(uvBounds(r).x, 8.0f, 0.02f);
}

TEST(Baking, UvlockIsIdentityForSidesUnderYRotation) {
    ElementDef cube;
    FaceDef f;
    f.uv = {3, 5, 9, 13};
    f.hasUv = true;
    BakedQuad locked, plain;
    ASSERT_TRUE(bakeFace(cube, Direction::North, f, pixelSprite(), blockStateRotation(0, 270), true, locked));
    ASSERT_TRUE(bakeFace(cube, Direction::North, f, pixelSprite(), blockStateRotation(0, 270), false, plain));
    for (int i = 0; i < 4; ++i) {
        EXPECT_NEAR(locked.uv[size_t(i)].x, plain.uv[size_t(i)].x, 1e-4f);
        EXPECT_NEAR(locked.uv[size_t(i)].y, plain.uv[size_t(i)].y, 1e-4f);
    }
}

TEST(Baking, RescaledCrossSpansTheBlock) {
    ElementDef plane;
    plane.from = {0.8f, 0, 8};
    plane.to = {15.2f, 16, 8};
    plane.hasRotation = true;
    plane.rotationOrigin = {8, 8, 8};
    plane.rotationAxis = Axis::Y;
    plane.rotationAngle = 45;
    plane.rescale = true;
    FaceDef f;
    BakedQuad q;
    ASSERT_TRUE(bakeFace(plane, Direction::North, f, pixelSprite(), glm::mat3(1.0f), false, q));
    float minX = 1, maxX = 0;
    for (const auto& p : q.pos) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        // The diagonal plane: x + z == 1 (x = 0.05 pairs with z = 0.95).
        EXPECT_NEAR(p.x + p.z, 1.0f, 1e-4f);
    }
    EXPECT_NEAR(minX, 0.05f, 1e-4f);
    EXPECT_NEAR(maxX, 0.95f, 1e-4f);
}

TEST(Baking, FaceUVRotation) {
    ElementDef cube;
    FaceDef f;
    f.uv = {0, 0, 16, 16};
    f.hasUv = true;
    f.rotation = 90;
    BakedQuad q;
    ASSERT_TRUE(bakeFace(cube, Direction::Up, f, pixelSprite(), glm::mat3(1.0f), false, q));
    // Rotation 90: vertex 0 takes the texel vertex 1 would have had without rotation.
    EXPECT_NEAR(q.uv[0].x, 0.016f, 0.02f);
    EXPECT_NEAR(q.uv[0].y, 15.98f, 0.03f);
}

TEST(Tints, VanillaColors) {
    EXPECT_EQ(redstoneColor(0), 0x4C0000u);
    EXPECT_EQ(redstoneColor(15), 0xFF3200u); // vanilla float math floors 0.19999999*255 to 50
    EXPECT_EQ(stemColor(0), 0x00FF00u);
    EXPECT_EQ(stemColor(7), 0xE0C71Cu);
    EXPECT_EQ(tintRuleFor("minecraft:birch_leaves").constant, 0x80A755u);
    EXPECT_EQ(tintRuleFor("minecraft:cherry_leaves").source, TintSource::None);
    const TintRule petals = tintRuleFor("minecraft:pink_petals");
    EXPECT_EQ(resolveTint(petals, 0, BiomeColors{}), 0xFFFFFFu);
    EXPECT_EQ(resolveTint(petals, 1, BiomeColors{}), BiomeColors{}.grass);
}

TEST(Tints, ColormapMatchesKnownBiomes) {
    auto img = Image::loadPng(kPackRoot / "assets/minecraft/textures/colormap/grass.png");
    ASSERT_TRUE(img);
    const Colormap grass(std::move(*img));
    EXPECT_EQ(grass.sample(0.8, 0.4), 0x91BD59u); // plains
    EXPECT_EQ(grass.sample(0.7, 0.8), 0x79C05Au); // forest
    auto fimg = Image::loadPng(kPackRoot / "assets/minecraft/textures/colormap/foliage.png");
    ASSERT_TRUE(fimg);
    EXPECT_EQ(Colormap(std::move(*fimg)).sample(0.8, 0.4), 0x77AB2Fu);
}
