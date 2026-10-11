#include "world/gen/BiomeLayout.h"

#include <algorithm>
#include <cmath>

namespace mc {
namespace {

using Name = std::string_view;

// [temperature][humidity]
constexpr Name kMiddle[5][5] = {
    {"snowy_plains", "snowy_plains", "snowy_plains", "snowy_taiga", "taiga"},
    {"plains", "plains", "forest", "taiga", "old_growth_spruce_taiga"},
    {"flower_forest", "plains", "forest", "birch_forest", "dark_forest"},
    {"savanna", "savanna", "forest", "jungle", "jungle"},
    {"desert", "desert", "desert", "desert", "desert"},
};
constexpr Name kMiddleVariant[5][5] = {
    {"ice_spikes", "", "snowy_taiga", "", ""},
    {"", "", "", "", "old_growth_pine_taiga"},
    {"sunflower_plains", "", "", "old_growth_birch_forest", ""},
    {"", "", "plains", "sparse_jungle", "bamboo_jungle"},
    {"", "", "", "", ""},
};
constexpr Name kPlateau[5][5] = {
    {"snowy_plains", "snowy_plains", "snowy_plains", "snowy_taiga", "snowy_taiga"},
    {"meadow", "meadow", "forest", "taiga", "old_growth_spruce_taiga"},
    {"meadow", "meadow", "meadow", "meadow", "dark_forest"},
    {"savanna_plateau", "savanna_plateau", "forest", "forest", "jungle"},
    {"badlands", "badlands", "badlands", "wooded_badlands", "wooded_badlands"},
};
constexpr Name kPlateauVariant[5][5] = {
    {"ice_spikes", "", "", "", ""},
    {"cherry_grove", "", "meadow", "meadow", "old_growth_pine_taiga"},
    {"cherry_grove", "cherry_grove", "forest", "birch_forest", ""},
    {"", "", "", "", ""},
    {"eroded_badlands", "eroded_badlands", "", "", ""},
};
constexpr Name kShattered[5][5] = {
    {"windswept_gravelly_hills", "windswept_gravelly_hills", "windswept_hills", "windswept_forest", "windswept_forest"},
    {"windswept_gravelly_hills", "windswept_gravelly_hills", "windswept_hills", "windswept_forest", "windswept_forest"},
    {"windswept_hills", "windswept_hills", "windswept_hills", "windswept_forest", "windswept_forest"},
    {"", "", "", "", ""},
    {"", "", "", "", ""},
};
constexpr Name kOcean[2][5] = {
    {"deep_frozen_ocean", "deep_cold_ocean", "deep_ocean", "deep_lukewarm_ocean", "warm_ocean"},
    {"frozen_ocean", "cold_ocean", "ocean", "lukewarm_ocean", "warm_ocean"},
};

int band(float v, const float* edges, int n) {
    for (int i = 0; i < n; ++i)
        if (v < edges[i]) return i;
    return n;
}

int tempIndex(float t) {
    static constexpr float e[4] = {-0.45f, -0.15f, 0.2f, 0.55f};
    return band(t, e, 4);
}
int humIndex(float h) {
    static constexpr float e[4] = {-0.35f, -0.1f, 0.1f, 0.3f};
    return band(h, e, 4);
}
int erosionIndex(float x) {
    static constexpr float e[6] = {-0.78f, -0.375f, -0.2225f, 0.05f, 0.45f, 0.55f};
    return band(x, e, 6);
}

struct Pick {
    int t, h;
    float w;
    Name middle() const {
        if (w >= 0 && !kMiddleVariant[t][h].empty()) return kMiddleVariant[t][h];
        return kMiddle[t][h];
    }
    Name badlands() const {
        if (h < 2) return w < 0 ? "badlands" : "eroded_badlands";
        return h < 3 ? "badlands" : "wooded_badlands";
    }
    Name middleOrBadlandsIfHot() const { return t == 4 ? badlands() : middle(); }
    Name middleOrBadlandsIfHotOrSlopeIfCold() const { return t == 0 ? slope() : middleOrBadlandsIfHot(); }
    Name plateau() const {
        if (w >= 0 && !kPlateauVariant[t][h].empty()) return kPlateauVariant[t][h];
        return kPlateau[t][h];
    }
    Name peak() const {
        if (t <= 2) return w < 0 ? "jagged_peaks" : "frozen_peaks";
        if (t == 3) return "stony_peaks";
        return badlands();
    }
    Name slope() const {
        if (t >= 3) return plateau();
        return h <= 1 ? "snowy_slopes" : "grove";
    }
    Name shattered() const {
        const Name s = kShattered[t][h];
        return s.empty() ? middle() : s;
    }
    Name windsweptSavanna(Name other) const {
        return (t > 1 && h < 4 && w >= 0) ? Name("windswept_savanna") : other;
    }
    Name beach() const {
        if (t == 0) return "snowy_beach";
        return t == 4 ? "desert" : "beach";
    }
    Name shatteredCoast() const { return windsweptSavanna(w >= 0 ? middle() : beach()); }
};

} // namespace

std::string_view pickOverworldBiome(const ClimatePoint& c) {
    // Underground biomes. Vanilla picks the nearest parameter point: surface biomes
    // sit at depth 0 and 1, the cave biomes span depth 0.2..0.9 with one narrowed
    // parameter each. A cave biome wins when its distance beats the surface
    // biome's depth distance (whose other parameters always match).
    {
        auto outside = [](float v, float lo, float hi) { return v < lo ? lo - v : (v > hi ? v - hi : 0.0f); };
        const float d = c.depth;
        const float surface = d <= 0.0f ? d * d : d >= 1.0f ? (d - 1.0f) * (d - 1.0f) : std::min(d * d, (1.0f - d) * (1.0f - d));
        const float depthTerm = outside(d, 0.2f, 0.9f) * outside(d, 0.2f, 0.9f);
        const float dripC = outside(c.continentalness, 0.8f, 1.0f), lushH = outside(c.humidity, 0.7f, 1.0f);
        const float drip = depthTerm + dripC * dripC, lush = depthTerm + lushH * lushH;
        if (std::min(drip, lush) < surface) return drip <= lush ? "dripstone_caves" : "lush_caves";
    }
    const float C = c.continentalness;
    const int t = tempIndex(c.temperature), h = humIndex(c.humidity), e = erosionIndex(c.erosion);
    if (C < -1.05f) return "mushroom_fields";
    if (C < -0.455f) return kOcean[0][t];
    if (C < -0.19f) return kOcean[1][t];

    const bool coast = C < -0.11f, near = !coast && C < 0.03f, mid = !coast && !near && C < 0.3f;
    const bool inland = !coast; // near, mid or far
    const bool midOrFar = !coast && !near;
    const Pick p{t, h, c.weirdness};
    const float aw = std::abs(c.weirdness);
    const bool frozen = t == 0;

    if (aw < 0.05f) { // valleys
        if (coast && e <= 1) return c.weirdness < 0 ? "stony_shore" : (frozen ? "frozen_river" : "river");
        if (midOrFar && e <= 1) return p.middleOrBadlandsIfHot();
        if (inland && e == 6) return frozen ? "frozen_river" : "swamp";
        return frozen ? "frozen_river" : "river";
    }
    if (aw < 0.26666668f) { // low slice
        if (coast) {
            if (e <= 2) return "stony_shore";
            if (e == 5) return p.shatteredCoast();
            return p.beach();
        }
        if (e <= 1) return near ? p.middleOrBadlandsIfHot() : p.middleOrBadlandsIfHotOrSlopeIfCold();
        if (e <= 3) return near ? p.middle() : p.middleOrBadlandsIfHot();
        if (e == 4) return p.middle();
        if (e == 5) return near ? p.windsweptSavanna(p.middle()) : p.middle();
        return frozen ? p.middle() : "swamp";
    }
    if (aw < 0.4f || aw >= 0.93333334f) { // mid slice
        if (coast && e <= 2) return "stony_shore";
        if (coast && (e == 3 || e == 4 || e == 6)) return p.beach();
        if (e == 0) return near ? p.middleOrBadlandsIfHotOrSlopeIfCold() : p.slope();
        if (e == 1) return near ? p.middleOrBadlandsIfHotOrSlopeIfCold() : (mid ? p.middleOrBadlandsIfHot() : p.slope());
        if (e == 2) return midOrFar ? p.plateau() : p.middle();
        if (e == 3) return mid ? p.middleOrBadlandsIfHot() : (near ? p.middle() : p.plateau());
        if (e == 4) return p.middle();
        if (e == 5) return coast ? p.shatteredCoast() : (near ? p.windsweptSavanna(p.middle()) : p.shattered());
        return (inland && !frozen) ? "swamp" : p.middle();
    }
    if (aw < 0.56666666f || aw >= 0.7666667f) { // high slice
        if (coast && e <= 1) return p.middle();
        if (e == 0) return near ? p.slope() : p.peak();
        if (e == 1) return near ? p.middleOrBadlandsIfHotOrSlopeIfCold() : p.slope();
        if (e == 2) return midOrFar ? p.plateau() : p.middle();
        if (e == 3) return mid ? p.middleOrBadlandsIfHot() : (near || coast ? p.middle() : p.plateau());
        if (e == 4) return p.middle();
        if (e == 5) return (coast || near) ? p.windsweptSavanna(p.middle()) : p.shattered();
        return p.middle();
    }
    // peaks
    if (e == 0) return p.peak();
    if (e == 1) return (coast || near) ? p.middleOrBadlandsIfHotOrSlopeIfCold() : p.peak();
    if (e == 2) return midOrFar ? p.plateau() : p.middle();
    if (e == 3) return mid ? p.middleOrBadlandsIfHot() : (coast || near ? p.middle() : p.plateau());
    if (e == 4) return p.middle();
    if (e == 5) return (coast || near) ? p.windsweptSavanna(p.shattered()) : p.shattered();
    return p.middle();
}

} // namespace mc
