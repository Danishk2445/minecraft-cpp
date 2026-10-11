#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/Image.h"
#include "entity/model/BlockEntityModels.h"
#include "entity/model/EntityModels.h"
#include "entity/model/HumanoidModel.h"
#include "resources/Atlas.h"
#include "resources/BlockModels.h"
#include "resources/ResourcePack.h"
#include "world/block/BlockRegistry.h"

using namespace mc;

namespace {

// The texel rectangle a baked quad reads, or (-1) if no quad faces `n`.
glm::vec4 uvRectFacing(const std::vector<ModelQuad>& quads, glm::vec3 n, int texW, int texH) {
    for (const ModelQuad& q : quads) {
        if (glm::dot(q.normal, n) < 0.99f) continue;
        glm::vec2 lo(1e9f), hi(-1e9f);
        for (const glm::vec2& uv : q.uv) {
            lo = glm::min(lo, uv);
            hi = glm::max(hi, uv);
        }
        return {lo.x * float(texW), lo.y * float(texH), hi.x * float(texW), hi.y * float(texH)};
    }
    return glm::vec4(-1.0f);
}

// Lowest and highest y of a set of quads.
glm::vec2 heightRange(const std::vector<ModelQuad>& quads) {
    glm::vec2 r(1e9f, -1e9f);
    for (const ModelQuad& q : quads)
        for (const glm::vec3& p : q.pos) r = {std::min(r.x, p.y), std::max(r.y, p.y)};
    return r;
}

// The normal of the quad reading texels [u0, v0, u1, v1) of a 64x64 texture.
glm::vec3 normalOfFace(const std::vector<ModelQuad>& quads, glm::vec4 rect) {
    for (const ModelQuad& q : quads) {
        glm::vec2 lo(1e9f), hi(-1e9f);
        for (const glm::vec2& uv : q.uv) lo = glm::min(lo, uv * 64.0f), hi = glm::max(hi, uv * 64.0f);
        if (glm::vec4(lo, hi) == rect) return q.normal;
    }
    return glm::vec3(0.0f);
}

} // namespace

TEST(ModelPart, BoxUvFollowsVanillaLayout) {
    ModelDefinition d;
    d.root.addChild("head").box({0, 0}, {-4, -8, -4}, {8, 8, 8});
    std::vector<ModelQuad> quads;
    bakeModel(d.root, glm::mat4(1.0f), 64, 64, quads);
    ASSERT_EQ(quads.size(), 6u);
    EXPECT_EQ(uvRectFacing(quads, {0, -1, 0}, 64, 64), glm::vec4(8, 0, 16, 8));
    EXPECT_EQ(uvRectFacing(quads, {0, 1, 0}, 64, 64), glm::vec4(16, 0, 24, 8));
    EXPECT_EQ(uvRectFacing(quads, {-1, 0, 0}, 64, 64), glm::vec4(0, 8, 8, 16));
    EXPECT_EQ(uvRectFacing(quads, {0, 0, -1}, 64, 64), glm::vec4(8, 8, 16, 16));
    EXPECT_EQ(uvRectFacing(quads, {1, 0, 0}, 64, 64), glm::vec4(16, 8, 24, 16));
    EXPECT_EQ(uvRectFacing(quads, {0, 0, 1}, 64, 64), glm::vec4(24, 8, 32, 16));

    // Mirrored boxes show the west texture on the east side.
    ModelDefinition m;
    m.root.addChild("arm").box({0, 0}, {-4, -8, -4}, {8, 8, 8}, 0.0f, true);
    quads.clear();
    bakeModel(m.root, glm::mat4(1.0f), 64, 64, quads);
    EXPECT_EQ(uvRectFacing(quads, {1, 0, 0}, 64, 64), glm::vec4(0, 8, 8, 16));

    // Flat boxes keep only their faces with area; onlyFaces limits the rest.
    ModelDefinition flat;
    flat.root.addChild("side").box({1, 0}, {0, 0, 0}, {14, 16, 0}).onlyFaces(1u << 2);
    quads.clear();
    bakeModel(flat.root, glm::mat4(1.0f), 16, 16, quads);
    ASSERT_EQ(quads.size(), 1u);
    EXPECT_EQ(uvRectFacing(quads, {0, 0, -1}, 16, 16), glm::vec4(1, 0, 15, 16));
}

// The UV-island test: every model reads texels inside its texture, at least
// some of them opaque, and when a set of models owns a texture, no opaque
// texel lies outside their faces (a misplaced box or texture offset shows up
// as uncovered texels).
TEST(EntityModels, UvIslandsMatchTextures) {
    const ResourcePack pack(MC_DEFAULT_PACK_DIR);
    const BlockEntityModels blockEntities;
    std::vector<ModelTextureUse> uses = blockEntities.textureUses();
    const ModelDefinition wide = playerModel(false), slim = playerModel(true), shield = shieldModel(),
                          trident = tridentModel();
    uses.push_back({"minecraft:entity/player/wide/steve", {&wide}, true});
    uses.push_back({"minecraft:entity/player/slim/alex", {&slim}, true});
    uses.push_back({"minecraft:entity/shield_base_nopattern", {&shield}, true});
    uses.push_back({"minecraft:entity/trident", {&trident}, false}); // also holds a small trident picture
    ASSERT_GE(uses.size(), 20u);

    for (const ModelTextureUse& use : uses) {
        SCOPED_TRACE(use.texture);
        std::string err;
        const auto img = Image::loadPng(pack.resource(ResourceLocation::parse(use.texture), "textures", ".png"), &err);
        ASSERT_TRUE(img) << err;
        std::vector<uint8_t> covered(size_t(img->width) * size_t(img->height), 0);
        for (const ModelDefinition* model : use.models) {
            const float sx = float(img->width) / float(model->texWidth);
            const float sy = float(img->height) / float(model->texHeight);
            for (const glm::ivec4& r : modelUvRects(*model)) {
                const int u0 = int(float(r.x) * sx), v0 = int(float(r.y) * sy);
                const int u1 = int(float(r.z) * sx), v1 = int(float(r.w) * sy);
                EXPECT_TRUE(u0 >= 0 && v0 >= 0 && u1 <= img->width && v1 <= img->height)
                    << "face reads " << u0 << "," << v0 << " to " << u1 << "," << v1;
                for (int v = std::max(v0, 0); v < std::min(v1, img->height); ++v)
                    for (int u = std::max(u0, 0); u < std::min(u1, img->width); ++u)
                        covered[size_t(v) * size_t(img->width) + size_t(u)] = 1;
            }
        }
        int inside = 0, outside = 0;
        for (int v = 0; v < img->height; ++v)
            for (int u = 0; u < img->width; ++u)
                if (img->alpha(u, v) > 0) {
                    if (covered[size_t(v) * size_t(img->width) + size_t(u)]) ++inside;
                    else ++outside;
                }
        EXPECT_GT(inside, 0);
        if (use.exclusive) {
            EXPECT_EQ(outside, 0) << "opaque texels outside every face";
        }
    }
}

// The player model in the world: feet on the ground, the hat's top at 32.5
// model units * 0.9375, and the face toward the body's yaw.
TEST(HumanoidModel, RestPoseStandsOnTheGroundFacingTheBodyYaw) {
    const ModelDefinition rest = playerModel(false);
    ModelDefinition posed = rest;
    HumanoidPose pose;
    pose.ageInTicks = 0.0f;
    animateHumanoid(posed.root, pose);
    std::vector<ModelQuad> quads;
    bakeModel(posed.root, livingModelTransform(0.0f, 0.9375f), 64, 64, quads);
    const glm::vec2 h = heightRange(quads);
    EXPECT_NEAR(h.x, -0.25f / 16.0f * 0.9375f, 0.002f); // the pants layer reaches 0.25 units below the feet
    EXPECT_NEAR(h.y, 1.905f, 0.002f);

    // The face (texels 8,8 to 16,16) looks along the body yaw: +z at 0, -x at 90.
    const glm::vec4 face(8, 8, 16, 16);
    quads.clear();
    bakeModel(*posed.root.find("head"), livingModelTransform(0.0f, 0.9375f), 64, 64, quads);
    const glm::vec3 south = normalOfFace(quads, face);
    EXPECT_NEAR(south.z, 1.0f, 1e-4f);
    quads.clear();
    bakeModel(*posed.root.find("head"), livingModelTransform(90.0f, 0.9375f), 64, 64, quads);
    EXPECT_NEAR(normalOfFace(quads, face).x, -1.0f, 1e-4f);
    // Looking down tips the face toward the ground.
    pose.headPitch = 45.0f;
    resetPose(posed.root, rest.root);
    animateHumanoid(posed.root, pose);
    quads.clear();
    bakeModel(*posed.root.find("head"), livingModelTransform(0.0f, 0.9375f), 64, 64, quads);
    const glm::vec3 down = normalOfFace(quads, face);
    EXPECT_NEAR(down.y, -0.7071f, 1e-3f);
    EXPECT_NEAR(down.z, 0.7071f, 1e-3f);
}

TEST(HumanoidModel, CrouchWalkAndSwingPoses) {
    const ModelDefinition rest = playerModel(false);
    ModelDefinition posed = rest;
    HumanoidPose pose;
    pose.crouching = true;
    animateHumanoid(posed.root, pose);
    EXPECT_FLOAT_EQ(posed.root.find("head")->pose.offset.y, 4.2f);
    EXPECT_FLOAT_EQ(posed.root.find("body")->pose.rotation.x, 0.5f);
    EXPECT_FLOAT_EQ(posed.root.find("right_leg")->pose.offset.z, 4.0f);
    EXPECT_FLOAT_EQ(posed.root.find("left_arm")->pose.offset.y, 2.0f + 3.2f);

    // Walking: legs swing opposite to each other and to the arm on their side.
    pose = {};
    pose.walkPosition = 0.0f;
    pose.walkSpeed = 1.0f;
    resetPose(posed.root, rest.root);
    animateHumanoid(posed.root, pose);
    EXPECT_FLOAT_EQ(posed.root.find("right_leg")->pose.rotation.x, 1.4f);
    EXPECT_FLOAT_EQ(posed.root.find("left_leg")->pose.rotation.x, -1.4f);
    EXPECT_NEAR(posed.root.find("right_arm")->pose.rotation.x, -1.0f, 1e-5f);

    // Halfway through a swing the right arm is raised (vanilla setupAttackAnimation).
    pose = {};
    pose.attackTime = 0.5f;
    resetPose(posed.root, rest.root);
    animateHumanoid(posed.root, pose);
    const float twist = std::sin(std::sqrt(0.5f) * 6.2831853f) * 0.2f;
    EXPECT_NEAR(posed.root.find("body")->pose.rotation.y, twist, 1e-5f);
    EXPECT_NEAR(posed.root.find("right_arm")->pose.rotation.x, -(std::sin(0.9375f * 3.1415927f) * 1.2f + 0.525f), 1e-4f);
    EXPECT_NEAR(posed.root.find("right_arm")->pose.offset.x, -std::cos(twist) * 5.0f, 1e-5f);
    // resetPose brings the rest pose back.
    resetPose(posed.root, rest.root);
    EXPECT_EQ(posed.root.find("right_arm")->pose.offset, glm::vec3(-5.0f, 2.0f, 0.0f));
    EXPECT_EQ(posed.root.find("body")->pose.rotation, glm::vec3(0.0f));
}

// Every block the models handle draws something in every state, with textures
// that are part of textures() (so they are in the block atlas) and exist in the pack.
TEST(BlockEntityModels, EveryStateHasTexturedPieces) {
    const ResourcePack pack(MC_DEFAULT_PACK_DIR);
    const Atlas atlas = *buildAtlas(pack, "blocks", 0);
    ModelStore store(pack);
    ModelBaker baker(store, atlas);
    BlockModels models;
    models.load(pack, store, baker);
    BlockRegistry reg;
    std::string err;
    reg.build(models, atlas, MC_DEFAULT_DATA_DIR, &err);
    const BlockEntityModels be;
    const std::vector<std::string> textures = be.textures();
    for (const std::string& t : textures)
        EXPECT_TRUE(std::filesystem::exists(pack.resource(ResourceLocation::parse(t), "textures", ".png"))) << t;
    int handled = 0, states = 0;
    std::vector<BlockEntityPiece> pieces;
    for (size_t b = 0; b < reg.blockCount(); ++b) {
        const Block& block = reg.block(b);
        if (!be.handles(block)) continue;
        ++handled;
        for (uint32_t i = 0; i < block.stateCount; ++i) {
            const StateId s = block.stateAt(i);
            pieces.clear();
            be.pieces(reg, s, pieces);
            ++states;
            ASSERT_FALSE(pieces.empty()) << reg.describe(s);
            for (const BlockEntityPiece& p : pieces) {
                ASSERT_NE(p.model, nullptr) << reg.describe(s);
                EXPECT_NE(std::find(textures.begin(), textures.end(), p.texture), textures.end())
                    << reg.describe(s) << " uses " << p.texture;
            }
        }
    }
    // Chests (chest, trapped, ender), beds (16), signs and hanging signs (4 kinds x 12
    // woods), banners (32), heads (14), shulker boxes (17), bell, pot, conduit.
    EXPECT_EQ(handled, 3 + 16 + 48 + 32 + 14 + 17 + 3);
    EXPECT_GT(states, 1000);
}
