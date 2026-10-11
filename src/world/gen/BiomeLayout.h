// BiomeLayout.h -- picks an overworld biome from climate parameters, following
// the structure of vanilla's OverworldBiomeBuilder (temperature/humidity tables,
// weirdness slices, continentalness and erosion bands). Deep dark, pale garden
// and mangrove swamp are not generated.
#pragma once

#include <string_view>

namespace mc {

struct ClimatePoint {
    float temperature = 0, humidity = 0, continentalness = 0, erosion = 0, weirdness = 0;
    float depth = 0; // > 0 below the surface (vanilla "depth" parameter)
};

// Returns a biome id without namespace, e.g. "plains".
std::string_view pickOverworldBiome(const ClimatePoint& c);

} // namespace mc
