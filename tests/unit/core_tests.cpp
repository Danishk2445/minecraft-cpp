#include <atomic>
#include <filesystem>
#include <mutex>
#include <vector>

#include <gtest/gtest.h>

#include "core/Image.h"
#include "core/JobSystem.h"
#include "core/Math.h"
#include "core/Rng.h"

using namespace mc;

namespace {
const std::filesystem::path kPack = std::filesystem::path(MC_DEFAULT_PACK_DIR) / "assets" / "minecraft";
}

// Golden values from a Python emulation of Java's 64-bit arithmetic, which also
// reproduces the well-known java.util.Random(0) and Random(42) outputs.
TEST(Rng, JavaRandomMatchesJava) {
    EXPECT_EQ(JavaRandom(0).nextInt(), -1155484576);
    EXPECT_EQ(JavaRandom(42).nextInt(), -1170105035);
    EXPECT_EQ(JavaRandom(0).nextLong(), -4962768465676381896LL);
    JavaRandom r(12345);
    const int expected[] = {1, 0, 1, 8, 5};
    for (int e : expected) EXPECT_EQ(r.nextInt(10), e);
    JavaRandom p(-7); // power-of-two bound takes the fast path
    const int expectedPow2[] = {4, 14, 10, 15, 8};
    for (int e : expectedPow2) EXPECT_EQ(p.nextInt(16), e);
}

TEST(Rng, PositionSeedMatchesMthGetSeed) {
    EXPECT_EQ(positionSeed(0, 0, 0), 0);
    EXPECT_EQ(positionSeed(1, 2, 3), -33674130277896LL);
    EXPECT_EQ(positionSeed(-100, 64, 250), -81216377325037LL);
    EXPECT_EQ(positionSeed(123456, -64, -98765), 133250059628185LL);
    EXPECT_EQ(positionSeed(29999999, 319, -29999999), -131906280443015LL);
}

TEST(Rng, XoroshiroMatchesReference) {
    Xoroshiro x(0);
    EXPECT_EQ(x.nextLong(), 3038984756725240190LL);
    EXPECT_EQ(x.nextLong(), -3694039286755638414LL);
    EXPECT_EQ(x.nextLong(), 4633751808701151732LL);
    Xoroshiro y(-123456789);
    EXPECT_EQ(y.nextLong(), -110953980331378620LL);
    Xoroshiro z(42);
    const int expected[] = {41, 31, 85, 48, 66, 28};
    for (int e : expected) EXPECT_EQ(z.nextInt(100), e);
}

TEST(Rng, BoundedValuesStayInRange) {
    Xoroshiro x(7);
    for (int i = 0; i < 100000; ++i) {
        const int v = x.nextInt(7);
        ASSERT_GE(v, 0);
        ASSERT_LT(v, 7);
        const float f = x.nextFloat();
        ASSERT_GE(f, 0.0f);
        ASSERT_LT(f, 1.0f);
    }
    JavaRandom j(99);
    for (int i = 0; i < 100000; ++i) {
        const int v = j.nextInt(1000);
        ASSERT_GE(v, 0);
        ASSERT_LT(v, 1000);
    }
}

TEST(Rng, DerivedStreamsAreStable) {
    const Xoroshiro base(1234);
    Xoroshiro a = base.at(10, 64, -5), b = base.at(10, 64, -5), c = base.at(11, 64, -5);
    EXPECT_EQ(a.nextLong(), b.nextLong());
    EXPECT_NE(base.at(10, 64, -5).nextLong(), c.nextLong());
    EXPECT_EQ(base.named("ores").nextLong(), base.named("ores").nextLong());
    EXPECT_NE(base.named("ores").nextLong(), base.named("trees").nextLong());
}

TEST(Math, Directions) {
    for (Direction d : kDirections) {
        EXPECT_EQ(opposite(opposite(d)), d);
        EXPECT_EQ(normal(opposite(d)), -normal(d));
        EXPECT_EQ(nearestDirection(glm::vec3(normal(d))), d);
        EXPECT_EQ(directionFromName(directionName(d)), d);
    }
    EXPECT_EQ(rotateYClockwise(Direction::North), Direction::East);
    EXPECT_EQ(normal(Direction::North), glm::ivec3(0, 0, -1));
    EXPECT_EQ(normal(Direction::East), glm::ivec3(1, 0, 0));
}

TEST(Math, FloorDivMod) {
    EXPECT_EQ(floorDiv(-1, 16), -1);
    EXPECT_EQ(floorDiv(-16, 16), -1);
    EXPECT_EQ(floorDiv(-17, 16), -2);
    EXPECT_EQ(floorDiv(15, 16), 0);
    EXPECT_EQ(floorMod(-1, 16), 15);
    EXPECT_EQ(floorMod(-16, 16), 0);
    EXPECT_EQ(ChunkPos::of(BlockPos(-1, 0, 16)), ChunkPos(-1, 1));
}

TEST(Image, PngRoundTrip) {
    Image img(5, 3);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 5; ++x) {
            uint8_t* p = img.px(x, y);
            p[0] = uint8_t(x * 40);
            p[1] = uint8_t(y * 80);
            p[2] = 7;
            p[3] = uint8_t(255 - x * 30);
        }
    const auto bytes = img.encodePng();
    ASSERT_FALSE(bytes.empty());
    const auto back = Image::decodePng(bytes);
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->width, 5);
    EXPECT_EQ(back->height, 3);
    EXPECT_EQ(back->pixels, img.pixels);
}

TEST(Image, DecodesEveryPackColorType) {
    // stone: palette; grass_block_top: grayscale; ascii font: 1-bit palette with tRNS.
    std::string err;
    const auto stone = Image::loadPng(kPack / "textures/block/stone.png", &err);
    ASSERT_TRUE(stone) << err;
    EXPECT_EQ(stone->width, 16);
    EXPECT_EQ(stone->height, 16);
    EXPECT_EQ(stone->alpha(3, 3), 255);

    const auto grass = Image::loadPng(kPack / "textures/block/grass_block_top.png", &err);
    ASSERT_TRUE(grass) << err;
    const uint8_t* g = grass->px(5, 5);
    EXPECT_EQ(g[0], g[1]); // gray expands to equal channels
    EXPECT_EQ(g[1], g[2]);

    const auto font = Image::loadPng(kPack / "textures/font/ascii.png", &err);
    ASSERT_TRUE(font) << err;
    EXPECT_EQ(font->width, 128);
    int transparent = 0, opaque = 0;
    for (size_t i = 3; i < font->pixels.size(); i += 4) (font->pixels[i] == 0 ? transparent : opaque)++;
    EXPECT_GT(transparent, 0);
    EXPECT_GT(opaque, 0);

    const auto water = Image::loadPng(kPack / "textures/block/water_still.png", &err);
    ASSERT_TRUE(water) << err;
    EXPECT_EQ(water->height, 512); // 32 animation frames
}

TEST(JobSystem, RunsAllJobsAndCallbacks) {
    JobSystem jobs(4);
    std::atomic<int> ran{0};
    int callbacks = 0; // main thread only
    for (int i = 0; i < 1000; ++i)
        jobs.submit(i % 7, [&] {
            ran.fetch_add(1);
            jobs.postMain([&] { ++callbacks; });
        });
    jobs.waitIdle();
    EXPECT_EQ(ran.load(), 1000);
    EXPECT_EQ(callbacks, 1000);
}

TEST(JobSystem, InlineModeRespectsPriority) {
    JobSystem jobs(0);
    std::vector<int> order;
    jobs.submit(5, [&] { order.push_back(5); });
    jobs.submit(1, [&] { order.push_back(1); });
    jobs.submit(3, [&] { order.push_back(3); });
    jobs.submit(1, [&] { order.push_back(11); }); // same priority: FIFO
    jobs.waitIdle();
    EXPECT_EQ(order, (std::vector<int>{1, 11, 3, 5}));
}

TEST(JobSystem, MoveOnlyCaptures) {
    JobSystem jobs(2);
    auto value = std::make_unique<int>(41);
    int result = 0;
    jobs.submit(0, [v = std::move(value), &jobs, &result]() mutable {
        const int computed = *v + 1;
        jobs.postMain([computed, &result] { result = computed; });
    });
    jobs.waitIdle();
    EXPECT_EQ(result, 42);
}
