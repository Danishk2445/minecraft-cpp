// Environment.h -- time of day, sky and fog colors and the 16x16 lightmap, as
// pure functions of the world state (vanilla formulas). The GL texture lives in
// render/Lightmap.
#pragma once

#include <array>
#include <cstdint>

#include <glm/glm.hpp>

namespace mc {

struct EnvironmentState {
    float timeOfDay = 0;   // celestial angle 0..1 (0 = noon)
    float skyDarken = 1;   // 0.2 (night) .. 1 (day)
    float sunBrightness = 1; // clamp(cos(2*pi*t) * 2 + 0.5, 0, 1)
    float starBrightness = 0;
    int moonPhase = 0;
    glm::vec3 skyColor{0.47f, 0.65f, 1.0f};
    glm::vec3 fogColor{0.75f, 0.85f, 1.0f};
    bool sunrise = false;
    glm::vec4 sunriseColor{0.0f};
    float rain = 0, thunder = 0;
    bool skyFlash = false; // the lightmap's sky at full strength
    // Fog distances (blocks).
    float fogEnvStart = 1e9f, fogEnvEnd = 1e9f + 1;
    float fogRdStart = 0, fogRdEnd = 0;
    float fogSkyEnd = 0;
    float fogCloudsEnd = 0;
};

struct EnvironmentInputs {
    int64_t dayTime = 6000;
    float partialTick = 0;
    glm::vec3 lookDir{0, 0, 1};
    int renderDistance = 12;
    uint32_t biomeSky = 0x78A7FF;
    uint32_t biomeFog = 0xC0D8FF;
    float rain = 0, thunder = 0;
    // Lightning's flash (vanilla skyFlashTime less the partial tick; >0 while it lights).
    float skyFlash = 0;
    // The fluid the camera is in (vanilla FogType): water and lava close the fog in.
    enum class CameraFluid : uint8_t { None, Water, Lava } cameraFluid = CameraFluid::None;
    uint32_t waterFogColor = 0x050533; // the biome's
    float waterVision = 0;             // 0..1: eyes adjusting under water (vanilla getWaterVision)
    bool closerWaterFog = false;       // swamps (vanilla #has_closer_water_fog)
    bool spectator = false;            // spectators see far in lava
};

float celestialAngle(int64_t dayTime, float partialTick);
// Vanilla Level.skyDarken (0 by day .. 11 at night): how many levels sky light loses
// for brightness checks (entity shadows; mob spawning later).
int skyDarkenLevel(int64_t dayTime, float rain = 0.0f, float thunder = 0.0f);
EnvironmentState computeEnvironment(const EnvironmentInputs& in);

// Port of shaders/core/lightmap.fsh: RGB 0..1 for [sky][block].
using LightmapTexels = std::array<glm::vec3, 256>;
LightmapTexels computeLightmap(const EnvironmentState& env, float gamma, float blockFlicker);

} // namespace mc
