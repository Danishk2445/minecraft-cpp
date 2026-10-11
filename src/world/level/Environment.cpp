#include "world/level/Environment.h"

#include <algorithm>
#include <cmath>

namespace mc {
namespace {

constexpr float kPi = 3.14159265358979f;

glm::vec3 rgb(uint32_t c) {
    return glm::vec3(float((c >> 16) & 255), float((c >> 8) & 255), float(c & 255)) / 255.0f;
}

float frac(double v) { return float(v - std::floor(v)); }

} // namespace

float celestialAngle(int64_t dayTime, float partialTick) {
    const double t = (double(dayTime % 24000) + partialTick) / 24000.0;
    const double d = frac(t - 0.25);
    const double e = 0.5 - std::cos(d * kPi) / 2.0;
    return float((d * 2.0 + e) / 3.0);
}

int skyDarkenLevel(int64_t dayTime, float rain, float thunder) {
    const double r = 1.0 - double(rain * 5.0f) / 16.0, t = 1.0 - double(thunder * 5.0f) / 16.0;
    const double c = 0.5 + 2.0 * std::clamp(double(std::cos(celestialAngle(dayTime, 1.0f) * 2.0f * kPi)), -0.25, 0.25);
    return int((1.0 - c * r * t) * 11.0);
}

EnvironmentState computeEnvironment(const EnvironmentInputs& in) {
    EnvironmentState s;
    s.rain = in.rain;
    s.thunder = in.thunder;
    s.timeOfDay = celestialAngle(in.dayTime, in.partialTick);
    const float c = std::cos(s.timeOfDay * 2.0f * kPi);

    // Level.getSkyDarken
    float g = std::clamp(1.0f - (c * 2.0f + 0.2f), 0.0f, 1.0f);
    g = 1.0f - g;
    g *= 1.0f - in.rain * 5.0f / 16.0f;
    g *= 1.0f - in.thunder * 5.0f / 16.0f;
    s.skyDarken = g * 0.8f + 0.2f;

    s.sunBrightness = std::clamp(c * 2.0f + 0.5f, 0.0f, 1.0f);
    // Level.getStarBrightness
    const float st = std::clamp(1.0f - (c * 2.0f + 0.25f), 0.0f, 1.0f);
    s.starBrightness = st * st * 0.5f;
    s.moonPhase = int((in.dayTime / 24000) % 8);

    // Level.getSkyColor
    glm::vec3 sky = rgb(in.biomeSky) * s.sunBrightness;
    if (in.rain > 0) {
        const float grey = (sky.r * 0.3f + sky.g * 0.59f + sky.b * 0.11f) * 0.6f;
        const float m = 1.0f - in.rain * 0.75f;
        sky = sky * m + glm::vec3(grey) * (1.0f - m);
    }
    if (in.thunder > 0) {
        const float grey = (sky.r * 0.3f + sky.g * 0.59f + sky.b * 0.11f) * 0.2f;
        const float m = 1.0f - in.thunder * 0.75f;
        sky = sky * m + glm::vec3(grey) * (1.0f - m);
    }
    s.skyColor = sky;

    // Sunrise / sunset (DimensionSpecialEffects)
    if (c >= -0.4f && c <= 0.4f) {
        const float gg = c / 0.4f * 0.5f + 0.5f;
        const float h = 1.0f - (1.0f - std::sin(gg * kPi)) * 0.99f;
        s.sunrise = true;
        s.sunriseColor = glm::vec4(gg * 0.3f + 0.7f, gg * gg * 0.7f + 0.2f, 0.2f, h * h);
    }

    // FogRenderer.computeFogColor (overworld, above water)
    const float b = s.sunBrightness;
    glm::vec3 fog = rgb(in.biomeFog) * glm::vec3(b * 0.94f + 0.06f, b * 0.94f + 0.06f, b * 0.91f + 0.09f);
    if (in.renderDistance >= 4 && s.sunrise) {
        // Facing the sun (which rises in the east at this angle) tints the fog.
        const float sunSide = std::sin(s.timeOfDay * 2.0f * kPi) > 0.0f ? -1.0f : 1.0f;
        float m = std::max(0.0f, glm::dot(in.lookDir, glm::vec3(sunSide, 0, 0)));
        m *= s.sunriseColor.a;
        fog = glm::mix(fog, glm::vec3(s.sunriseColor), m);
    }
    const float f = 1.0f - std::pow(0.25f + 0.75f * float(in.renderDistance) / 32.0f, 0.25f);
    fog += (sky - fog) * f;
    if (in.rain > 0) fog *= glm::vec3(1.0f - in.rain * 0.5f, 1.0f - in.rain * 0.5f, 1.0f - in.rain * 0.4f);
    if (in.thunder > 0) fog *= 1.0f - in.thunder * 0.5f;
    s.fogColor = fog;

    // Fog distances: render-distance fog over the last ~10% of the view, atmospheric
    // fog only in rain.
    const float rd = float(in.renderDistance * 16);
    s.fogRdStart = rd - std::clamp(rd / 10.0f, 4.0f, 64.0f);
    s.fogRdEnd = rd;
    s.fogSkyEnd = rd;
    s.fogCloudsEnd = std::max(rd, 192.0f) * 2.0f;
    if (in.rain > 0) {
        s.fogEnvStart = glm::mix(1e4f, rd * 0.25f, in.rain);
        s.fogEnvEnd = glm::mix(1e4f + 1, rd, in.rain);
    }
    return s;
}

LightmapTexels computeLightmap(const EnvironmentState& env, float gamma, float blockFlicker) {
    LightmapTexels texels{};
    // Uniform values as computed by vanilla LightTexture.updateLightTexture.
    const float ambient = 0.0f; // overworld
    const float skyFactor = env.skyDarken * 0.95f + 0.05f;
    const float blockFactor = blockFlicker + 1.5f;
    const glm::vec3 skyLightColor = glm::mix(glm::vec3(env.skyDarken, env.skyDarken, 1.0f), glm::vec3(1.0f), 0.35f);
    auto brightness = [&](float level) {
        const float curved = level / (4.0f - 3.0f * level);
        return glm::mix(curved, 1.0f, ambient);
    };
    for (int sky = 0; sky < 16; ++sky)
        for (int blk = 0; blk < 16; ++blk) {
            const float bb = brightness(float(blk) / 15.0f) * blockFactor;
            const float sb = brightness(float(sky) / 15.0f) * skyFactor;
            glm::vec3 color(bb, bb * ((bb * 0.6f + 0.4f) * 0.6f + 0.4f), bb * (bb * bb * 0.6f + 0.4f));
            color += skyLightColor * sb;
            color = glm::mix(color, glm::vec3(0.75f), 0.04f);
            // Vanilla's DarknessScale step (darkness = 0 here) clamps to [0,1]; the
            // "notGamma" curve below goes negative for values above ~2 without it.
            color = glm::clamp(color, 0.0f, 1.0f);
            const glm::vec3 nx = glm::vec3(1.0f) - color;
            const glm::vec3 notGamma = glm::vec3(1.0f) - nx * nx * nx * nx;
            color = glm::mix(color, notGamma, gamma);
            color = glm::mix(color, glm::vec3(0.75f), 0.04f);
            color = glm::clamp(color, 0.0f, 1.0f);
            texels[size_t(sky * 16 + blk)] = color;
        }
    return texels;
}

} // namespace mc
