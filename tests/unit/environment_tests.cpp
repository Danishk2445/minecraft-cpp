#include <gtest/gtest.h>

#include "world/level/Environment.h"

using namespace mc;

TEST(Environment, CelestialAngle) {
    EXPECT_NEAR(celestialAngle(6000, 0), 0.0f, 1e-6f);  // noon
    EXPECT_NEAR(celestialAngle(18000, 0), 0.5f, 1e-6f); // midnight
    EXPECT_NEAR(celestialAngle(6000 + 24000 * 5, 0), 0.0f, 1e-6f);
    // Morning: the sun is rising (between midnight and noon).
    const float morning = celestialAngle(0, 0);
    EXPECT_GT(morning, 0.5f);
    EXPECT_LT(morning, 1.0f);
}

TEST(Environment, SkyDarkenAndStars) {
    EnvironmentInputs in;
    in.dayTime = 6000;
    const EnvironmentState noon = computeEnvironment(in);
    EXPECT_NEAR(noon.skyDarken, 1.0f, 1e-5f);
    EXPECT_NEAR(noon.starBrightness, 0.0f, 1e-6f);
    EXPECT_NEAR(noon.skyColor.r, 0x78 / 255.0f, 1e-3f); // plains sky #78A7FF at full brightness
    EXPECT_FALSE(noon.sunrise);
    in.dayTime = 18000;
    const EnvironmentState night = computeEnvironment(in);
    EXPECT_NEAR(night.skyDarken, 0.2f, 1e-5f);
    EXPECT_NEAR(night.starBrightness, 0.5f, 1e-5f);
    EXPECT_NEAR(night.skyColor.b, 0.0f, 1e-5f);
    EXPECT_EQ(night.moonPhase, 0);
    in.dayTime = 23500; // dawn glow
    EXPECT_TRUE(computeEnvironment(in).sunrise);
}

TEST(Environment, LightmapDayNight) {
    EnvironmentInputs in;
    in.dayTime = 6000;
    const auto day = computeLightmap(computeEnvironment(in), 0.5f, 0.0f);
    in.dayTime = 18000;
    const auto night = computeLightmap(computeEnvironment(in), 0.5f, 0.0f);
    auto luma = [](const glm::vec3& c) { return c.r * 0.299f + c.g * 0.587f + c.b * 0.114f; };
    const glm::vec3 daySky = day[15 * 16 + 0], nightSky = night[15 * 16 + 0];
    EXPECT_GT(luma(daySky), 0.95f);
    EXPECT_GT(luma(daySky) / luma(nightSky), 1.8f);
    EXPECT_GT(nightSky.b, nightSky.r); // moonlight is blue
    const glm::vec3 torch = night[0 * 16 + 10];
    EXPECT_GT(torch.r, torch.b + 0.05f); // mid-level block light is warm
    // Full light saturates (vanilla's final 4% mix toward 0.75 caps it at 0.99), never black.
    EXPECT_NEAR(day[15 * 16 + 15].r, 0.99f, 1e-3f);
    // Darkness is not total black (vanilla mixes 4% toward 0.75 twice).
    EXPECT_GT(night[0].r, 0.03f);
    // Brighter with the Brightness option.
    const auto bright = computeLightmap(computeEnvironment(in), 1.0f, 0.0f);
    EXPECT_GT(luma(bright[8 * 16]), luma(night[8 * 16]));
}
