#include <cmath>
#include <memory>
#include <unordered_map>

#include <gtest/gtest.h>

#include "entity/Particles.h"
#include "entity/Physics.h"
#include "entity/Player.h"
#include "resources/Atlas.h"
#include "resources/BlockModels.h"
#include "resources/ResourcePack.h"
#include "world/block/BlockRegistry.h"
#include "world/level/Raycast.h"

using namespace mc;

namespace {

// Blocks by position; everything else is air.
class TestWorld final : public BlockGetter {
public:
    explicit TestWorld(const BlockRegistry& reg) : reg_(reg) {}
    const BlockRegistry& registry() const override { return reg_; }
    StateId getBlock(const BlockPos& p) const override {
        auto it = blocks_.find(p);
        return it == blocks_.end() ? StateId(0) : it->second;
    }
    void set(const BlockPos& p, StateId s) { blocks_[p] = s; }
    void floor(int y, StateId s, int radius = 64) {
        for (int z = -radius; z <= radius; ++z)
            for (int x = -radius; x <= radius; ++x) set({x, y, z}, s);
    }

private:
    const BlockRegistry& reg_;
    std::unordered_map<BlockPos, StateId> blocks_;
};

struct PhysicsFixture : ::testing::Test {
    static void SetUpTestSuite() {
        pack = std::make_unique<ResourcePack>(MC_DEFAULT_PACK_DIR);
        atlas = std::make_unique<Atlas>(*buildAtlas(*pack, "blocks", 0));
        store = std::make_unique<ModelStore>(*pack);
        baker = std::make_unique<ModelBaker>(*store, *atlas);
        models = std::make_unique<BlockModels>();
        models->load(*pack, *store, *baker);
        registry = std::make_unique<BlockRegistry>();
        std::string err;
        registry->build(*models, *atlas, MC_DEFAULT_DATA_DIR, &err);
    }
    static void TearDownTestSuite() {
        registry.reset();
        models.reset();
        baker.reset();
        store.reset();
        atlas.reset();
        pack.reset();
    }
    static StateId S(const char* text) { return *registry->parse(text); }

    // A player standing on a stone floor at y = 0 (feet at y = 1), facing +z.
    static Player standing(TestWorld& w) {
        Player p;
        p.pos = {0.5, 1.0, 0.5};
        p.onGround = true;
        PlayerInput idle;
        for (int i = 0; i < 5; ++i) p.tick(w, idle);
        return p;
    }
    // Horizontal speed in m/s once the velocity has settled.
    static double settledSpeed(TestWorld& w, Player& p, const PlayerInput& in) {
        for (int i = 0; i < 60; ++i) p.tick(w, in);
        const glm::dvec3 start = p.pos;
        for (int i = 0; i < 20; ++i) p.tick(w, in);
        return glm::length(glm::dvec2(p.pos.x - start.x, p.pos.z - start.z));
    }

    static inline std::unique_ptr<ResourcePack> pack;
    static inline std::unique_ptr<Atlas> atlas;
    static inline std::unique_ptr<ModelStore> store;
    static inline std::unique_ptr<ModelBaker> baker;
    static inline std::unique_ptr<BlockModels> models;
    static inline std::unique_ptr<BlockRegistry> registry;
};

} // namespace

TEST_F(PhysicsFixture, JumpApex) {
    TestWorld w(*registry);
    w.floor(0, S("stone"));
    Player p = standing(w);
    ASSERT_TRUE(p.onGround);
    EXPECT_DOUBLE_EQ(p.pos.y, 1.0);
    PlayerInput jump;
    jump.jump = true;
    double apex = p.pos.y;
    p.tick(w, jump);
    PlayerInput idle;
    for (int i = 0; i < 30; ++i) {
        apex = std::max(apex, p.pos.y);
        p.tick(w, idle);
    }
    EXPECT_NEAR(apex - 1.0, 1.2522, 0.001);
    EXPECT_TRUE(p.onGround);
    EXPECT_DOUBLE_EQ(p.pos.y, 1.0);
}

TEST_F(PhysicsFixture, WalkAndSprintSpeeds) {
    TestWorld w(*registry);
    w.floor(0, S("stone"), 200);
    PlayerInput walk;
    walk.forward = 1.0f;
    Player p = standing(w);
    EXPECT_NEAR(settledSpeed(w, p, walk), 4.317, 0.01);
    PlayerInput sprint = walk;
    sprint.sprint = true;
    Player q = standing(w);
    EXPECT_NEAR(settledSpeed(w, q, sprint), 5.612, 0.01);
    EXPECT_TRUE(q.sprinting);
    PlayerInput sneak = walk;
    sneak.sneak = true;
    Player r = standing(w);
    EXPECT_NEAR(settledSpeed(w, r, sneak), 4.317 * 0.3, 0.02);
    // Walking moves along the facing direction (yaw 0 = +z).
    EXPECT_GT(p.pos.z, 10.0);
    EXPECT_NEAR(p.pos.x, 0.5, 1e-6);
}

// M13 view bobbing (vanilla walkDist and bob): 0.6 per block walked; the bob eases to the
// ground speed, capped at 0.1, and dies away in the air and standing still.
TEST_F(PhysicsFixture, ViewBobbingFollowsTheWalk) {
    TestWorld w(*registry);
    w.floor(0, S("stone"), 200);
    PlayerInput walk;
    walk.forward = 1.0f;
    Player p = standing(w);
    const glm::dvec3 start = p.pos;
    for (int i = 0; i < 40; ++i) p.tick(w, walk);
    EXPECT_NEAR(p.walkDist, glm::distance(p.pos, start) * 0.6, 1e-3);
    EXPECT_NEAR(p.bob, 0.1f, 1e-3f); // walking is 0.216 blocks a tick
    EXPECT_NEAR(p.walkDist - p.walkDistO, 4.317 / 20.0 * 0.6, 0.01);
    PlayerInput jump = walk;
    jump.jump = true;
    p.tick(w, jump);
    for (int i = 0; i < 5; ++i) p.tick(w, walk);
    EXPECT_LT(p.bob, 0.01f);
    for (int i = 0; i < 40; ++i) p.tick(w, PlayerInput{});
    EXPECT_LT(p.bob, 1e-4f);
}

TEST_F(PhysicsFixture, StepsOntoSlabsButNotFullBlocks) {
    TestWorld w(*registry);
    w.floor(0, S("stone"));
    for (int z = 3; z <= 30; ++z)
        for (int x = -5; x <= 5; ++x) w.set({x, 1, z}, S("oak_slab[type=bottom]"));
    PlayerInput walk;
    walk.forward = 1.0f;
    Player p = standing(w);
    for (int i = 0; i < 40; ++i) p.tick(w, walk);
    EXPECT_GT(p.pos.z, 4.0);
    EXPECT_DOUBLE_EQ(p.pos.y, 1.5);
    TestWorld wall(*registry);
    wall.floor(0, S("stone"));
    for (int x = -5; x <= 5; ++x) wall.set({x, 1, 3}, S("stone"));
    Player q = standing(wall);
    for (int i = 0; i < 40; ++i) q.tick(wall, walk);
    EXPECT_NEAR(q.pos.z, 3.0 - 0.3, 1e-6); // pressed against the block
    EXPECT_DOUBLE_EQ(q.pos.y, 1.0);
    EXPECT_FALSE(q.sprinting);
}

TEST_F(PhysicsFixture, SneakingStopsAtLedges) {
    TestWorld w(*registry);
    for (int z = -3; z <= 1; ++z)
        for (int x = -3; x <= 3; ++x) w.set({x, 0, z}, S("stone"));
    PlayerInput sneak;
    sneak.forward = 1.0f;
    sneak.sneak = true;
    Player p = standing(w);
    for (int i = 0; i < 100; ++i) p.tick(w, sneak);
    EXPECT_DOUBLE_EQ(p.pos.y, 1.0);
    EXPECT_LE(p.pos.z, 2.0 + 0.3); // the box may hang over the edge, the feet do not leave it
    PlayerInput walk;
    walk.forward = 1.0f;
    for (int i = 0; i < 40; ++i) p.tick(w, walk);
    EXPECT_LT(p.pos.y, 0.0); // without sneaking it walks off
}

TEST_F(PhysicsFixture, ClimbsLaddersAndFliesInCreative) {
    TestWorld w(*registry);
    w.floor(0, S("stone"));
    for (int y = 1; y <= 10; ++y) {
        w.set({0, y, 1}, S("stone"));
        w.set({0, y, 0}, S("ladder[facing=north]"));
    }
    Player p = standing(w);
    PlayerInput climb;
    climb.forward = 1.0f;
    for (int i = 0; i < 40; ++i) p.tick(w, climb);
    // (0.2 - gravity) * drag = 0.1176 blocks per tick: vanilla's 2.35 m/s.
    EXPECT_GT(p.pos.y, 1.0 + 0.1176 * 35);
    EXPECT_LT(p.pos.y, 1.0 + 0.1176 * 40 + 0.01);
    // Creative flight: about 10.9 m/s, double that sprinting; landing ends it.
    TestWorld air(*registry);
    air.floor(0, S("stone"));
    Player f = standing(air);
    f.setMode(GameMode::Creative);
    PlayerInput toggle;
    toggle.doubleTapJump = true;
    toggle.jump = true;
    f.tick(air, toggle);
    ASSERT_TRUE(f.flying);
    PlayerInput up;
    up.jump = true;
    for (int i = 0; i < 10; ++i) f.tick(air, up);
    EXPECT_GT(f.pos.y, 3.0);
    PlayerInput fly;
    fly.forward = 1.0f;
    EXPECT_NEAR(settledSpeed(air, f, fly), 10.92, 0.1);
    PlayerInput down;
    down.sneak = true;
    for (int i = 0; i < 200 && f.flying; ++i) f.tick(air, down);
    EXPECT_FALSE(f.flying);
    EXPECT_DOUBLE_EQ(f.pos.y, 1.0);
}

TEST_F(PhysicsFixture, Swimming) {
    TestWorld w(*registry);
    w.floor(0, S("stone"));
    for (int y = 1; y <= 8; ++y)
        for (int z = -3; z <= 3; ++z)
            for (int x = -3; x <= 3; ++x) w.set({x, y, z}, S("water"));
    Player p;
    p.pos = {0.5, 6.0, 0.5};
    PlayerInput idle;
    for (int i = 0; i < 20; ++i) p.tick(w, idle);
    EXPECT_TRUE(p.inWater);
    EXPECT_LT(p.pos.y, 6.0);
    EXPECT_GT(p.pos.y, 6.0 - 20 * 0.03); // sinks slowly
    PlayerInput swim;
    swim.jump = true;
    const double before = p.pos.y;
    for (int i = 0; i < 20; ++i) p.tick(w, swim);
    EXPECT_GT(p.pos.y, before + 0.5);
}

TEST_F(PhysicsFixture, CollisionAxisOrder) {
    // Sliding along a wall keeps the free axis.
    TestWorld w(*registry);
    for (int z = -5; z <= 5; ++z) w.set({2, 0, z}, S("stone"));
    const AABB box(0.7, 0.0, 0.2, 1.3, 1.8, 0.8);
    const glm::dvec3 moved = collideBoundingBox(w, box, {1.0, 0.0, 0.5});
    EXPECT_NEAR(moved.x, 0.7, 1e-9);
    EXPECT_NEAR(moved.z, 0.5, 1e-9);
    // A fence is 1.5 blocks tall for collision.
    TestWorld f(*registry);
    f.set({0, 0, 0}, S("oak_fence"));
    const glm::dvec3 drop = collideBoundingBox(f, AABB(0.2, 3.0, 0.2, 0.8, 4.8, 0.8), {0.0, -5.0, 0.0});
    EXPECT_NEAR(3.0 + drop.y, 1.5, 1e-9);
}

TEST_F(PhysicsFixture, BreakParticles) {
    TestWorld w(*registry);
    w.floor(0, S("stone"));
    ParticleSystem ps;
    ps.addBlockBreak(*registry, S("stone"), {0, 1, 0}, 0, 0xFFFFFF);
    EXPECT_EQ(ps.particles().size(), 64u); // a 4x4x4 grid over the full block
    ps.addBlockBreak(*registry, S("oak_slab[type=bottom]"), {3, 1, 0}, 0, 0xFFFFFF);
    EXPECT_EQ(ps.particles().size(), 64u + 32u); // half as tall: 4x2x4
    for (const Particle& p : ps.particles()) {
        EXPECT_GE(p.lifetime, 4);
        EXPECT_LE(p.lifetime, 40);
        EXPECT_GT(p.vel.y, -0.2); // thrown out and up a little
    }
    // They land on the floor and expire.
    for (int i = 0; i < 10; ++i) ps.tick(w);
    for (const Particle& p : ps.particles()) EXPECT_GE(p.pos.y, 1.0 - 1e-9);
    for (int i = 0; i < 40; ++i) ps.tick(w);
    EXPECT_TRUE(ps.particles().empty());
    // Mining chips start just outside the hit face.
    ps.addBlockHit(*registry, S("stone"), {0, 1, 0}, Direction::Up, 0, 0xFFFFFF);
    ASSERT_EQ(ps.particles().size(), 1u);
    EXPECT_NEAR(ps.particles()[0].pos.y, 2.1, 1e-9);
}

// M13 Particles option (vanilla addParticleInternal): with a camera, nothing past 32 blocks,
// a third dropped on Decreased, none on Minimal; block debris always shows.
TEST_F(PhysicsFixture, ParticlesOptionThinsLevelParticles) {
    ParticleSystem ps;
    ps.setSprites(ParticleType::Smoke, {0});
    ps.camera = glm::dvec3(0.0);
    for (int i = 0; i < 300; ++i) ps.add(ParticleType::Smoke, {1.0, 1.0, 1.0});
    EXPECT_EQ(ps.particles().size(), 300u);
    ps.add(ParticleType::Smoke, {40.0, 0.0, 0.0}); // too far
    EXPECT_EQ(ps.particles().size(), 300u);
    ps.mode = ParticleMode::Decreased;
    for (int i = 0; i < 300; ++i) ps.add(ParticleType::Smoke, {1.0, 1.0, 1.0});
    const size_t decreased = ps.particles().size() - 300;
    EXPECT_GT(decreased, 160u); // two in three
    EXPECT_LT(decreased, 240u);
    ps.mode = ParticleMode::Minimal;
    const size_t before = ps.particles().size();
    for (int i = 0; i < 100; ++i) ps.add(ParticleType::Smoke, {1.0, 1.0, 1.0});
    EXPECT_EQ(ps.particles().size(), before);
    ps.addBlockBreak(*registry, S("stone"), {0, 1, 0}, 0, 0xFFFFFF);
    EXPECT_EQ(ps.particles().size(), before + 64u);
}

// Vanilla LivingEntity animation state: limb swing from movement, the body
// following the movement within 50 degrees of the head, 6-tick arm swings.
TEST_F(PhysicsFixture, WalkAnimationBodyTurnAndSwing) {
    TestWorld w(*registry);
    w.floor(0, S("stone"), 200);
    PlayerInput walk;
    walk.forward = 1.0f;
    Player p = standing(w);
    EXPECT_FLOAT_EQ(p.walkAnimation.speed, 0.0f);
    for (int i = 0; i < 40; ++i) p.tick(w, walk);
    EXPECT_NEAR(p.walkAnimation.speed, std::min(4.317f / 20.0f * 4.0f, 1.0f), 0.01f);
    EXPECT_NEAR(p.bodyYaw, 0.0f, 0.01f);
    EXPECT_FLOAT_EQ(p.headYaw, p.yaw);

    // Strafing left (+x while facing +z): the body turns toward -90 but stops 50 short of the head.
    PlayerInput strafe;
    strafe.strafe = 1.0f;
    for (int i = 0; i < 40; ++i) p.tick(w, strafe);
    EXPECT_NEAR(p.bodyYaw, -50.0f, 0.01f);
    // Walking backwards keeps the body facing forward.
    PlayerInput back;
    back.forward = -1.0f;
    for (int i = 0; i < 60; ++i) p.tick(w, back);
    EXPECT_NEAR(wrapDegrees(p.bodyYaw), 0.0f, 0.5f);
    // Standing still, the limbs come to rest.
    for (int i = 0; i < 60; ++i) p.tick(w, PlayerInput{});
    EXPECT_LT(p.walkAnimation.speed, 0.01f);

    // A swing runs 6 ticks; meanwhile the body turns to the look.
    p.yaw = 120.0f;
    p.swing();
    const float expected[] = {0.0f, 1.0f / 6, 2.0f / 6, 3.0f / 6, 4.0f / 6, 5.0f / 6};
    for (float e : expected) {
        p.tick(w, PlayerInput{});
        EXPECT_NEAR(p.attackAnim(1.0f), e, 1e-6f);
    }
    EXPECT_GT(wrapDegrees(p.bodyYaw), 100.0f);
    // When it ends, interpolation runs on to 1 instead of jumping back, then rests.
    p.tick(w, PlayerInput{});
    EXPECT_NEAR(p.attackAnim(0.5f), 5.0f / 6 + 0.5f / 6, 1e-6f);
    p.tick(w, PlayerInput{});
    EXPECT_NEAR(p.attackAnim(0.5f), 0.0f, 1e-6f);
}

TEST_F(PhysicsFixture, ViewVectorFollowsYawAndPitch) {
    Player p;
    p.setRotation(0.0f, 0.0f);
    EXPECT_NEAR(p.viewVector().z, 1.0, 1e-9);
    p.setRotation(90.0f, 0.0f);
    EXPECT_NEAR(p.viewVector().x, -1.0, 1e-9);
    p.setRotation(0.0f, 90.0f);
    EXPECT_NEAR(p.viewVector().y, -1.0, 1e-9);
}

// Vanilla Camera.getMaxZoom: 4 blocks back unless a block's visual shape is closer.
TEST_F(PhysicsFixture, ThirdPersonCameraStopsShortOfBlocks) {
    TestWorld w(*registry);
    const glm::dvec3 eye(0.5, 10.62, 0.5);
    const glm::dvec3 forward(0.0, 0.0, 1.0); // looking south: the camera backs away north
    EXPECT_DOUBLE_EQ(cameraDistance(w, eye, forward, 4.0), 4.0);
    // A wall whose south face is at z = -2: the corner rays stop there, measured from the eye.
    for (int y = 8; y <= 13; ++y)
        for (int x = -2; x <= 2; ++x) w.set({x, y, -3}, S("stone"));
    EXPECT_NEAR(cameraDistance(w, eye, forward, 4.0), std::sqrt(2.5 * 2.5 + 0.02), 1e-9);
    // Glass and panes have no visual shape.
    for (int y = 8; y <= 13; ++y)
        for (int x = -2; x <= 2; ++x) w.set({x, y, -3}, S("glass"));
    EXPECT_DOUBLE_EQ(cameraDistance(w, eye, forward, 4.0), 4.0);
    w.set({0, 10, -1}, S("oak_slab[type=top]"));
    // The top slab spans y 10.5..11; the upper corner rays (y 10.72) hit its south face at z = 0.
    EXPECT_NEAR(cameraDistance(w, eye, forward, 4.0), std::sqrt(0.5 * 0.5 + 0.02), 1e-9);
}
