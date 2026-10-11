#include "world/biome/Biome.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "core/Image.h"
#include "core/Json.h"
#include "core/Noise.h"
#include "resources/ResourcePack.h"

namespace mc {
namespace {

std::optional<uint32_t> parseColor(const Json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_string()) return std::nullopt;
    return uint32_t(std::stoul(it->get<std::string>(), nullptr, 16));
}

uint32_t hsvToRgb(float h, float s, float v) {
    const int i = int(h * 6.0f) % 6;
    const float f = h * 6.0f - float(int(h * 6.0f));
    const float p = v * (1.0f - s), q = v * (1.0f - f * s), t = v * (1.0f - (1.0f - f) * s);
    float r = 0, g = 0, b = 0;
    switch (i) {
    case 0: r = v, g = t, b = p; break;
    case 1: r = q, g = v, b = p; break;
    case 2: r = p, g = v, b = t; break;
    case 3: r = p, g = q, b = v; break;
    case 4: r = t, g = p, b = v; break;
    default: r = v, g = p, b = q; break;
    }
    auto c = [](float x) { return uint32_t(std::clamp(int(x * 255.0f), 0, 255)); };
    return (c(r) << 16) | (c(g) << 8) | c(b);
}

const PerlinNoise& swampNoise() {
    static const PerlinNoise noise(Xoroshiro(2345), 0, {1.0});
    return noise;
}

} // namespace

uint32_t skyColorForTemperature(float temperature) {
    const float f = std::clamp(temperature / 3.0f, -1.0f, 1.0f);
    return hsvToRgb(0.62222224f - f * 0.05f, 0.5f + f * 0.1f, 1.0f);
}

bool Biome::coldAt(int y) const {
    const float t = y > 80 ? temperature - float(y - 80) * 0.05f / 40.0f : temperature;
    return t < 0.15f;
}

bool BiomeRegistry::load(const std::filesystem::path& dataDir, const std::filesystem::path& packRoot, std::string* error) {
    std::string err;
    auto j = loadJsonFile(dataDir / "worldgen" / "biomes.json", &err);
    if (!j || !j->is_object()) {
        if (error) *error = err.empty() ? "biomes.json: expected an object" : err;
        return false;
    }
    ResourcePack pack(packRoot);
    auto colormap = [&](const char* name) {
        auto img = Image::loadPng(pack.file("minecraft", std::format("textures/colormap/{}.png", name)));
        return img ? Colormap(std::move(*img)) : Colormap();
    };
    const Colormap grass = colormap("grass"), foliage = colormap("foliage"), dry = colormap("dry_foliage");

    biomes_.clear();
    index_.clear();
    for (auto it = j->begin(); it != j->end(); ++it) {
        if (it.key().starts_with('_')) continue;
        const Json& b = *it;
        Biome bio;
        bio.id = "minecraft:" + it.key();
        bio.index = BiomeId(biomes_.size());
        bio.temperature = jsonGet<float>(b, "temperature", 0.5f);
        bio.downfall = jsonGet<float>(b, "downfall", 0.5f);
        bio.precipitation = jsonGet<bool>(b, "precipitation", true);
        bio.waterColor = parseColor(b, "water").value_or(0x3F76E4);
        bio.waterFogColor = parseColor(b, "water_fog").value_or(0x050533);
        bio.fogColor = parseColor(b, "fog").value_or(0xC0D8FF);
        bio.skyColor = parseColor(b, "sky").value_or(skyColorForTemperature(bio.temperature));
        const std::string mod = jsonGet<std::string>(b, "grass_modifier", "");
        bio.grassModifier = mod == "swamp" ? GrassModifier::Swamp
                            : mod == "dark_forest" ? GrassModifier::DarkForest
                                                   : GrassModifier::None;
        bio.surface = jsonGet<std::string>(b, "surface", "grass");
        bio.features = jsonGet<std::string>(b, "features", "none");
        const double t = std::clamp(bio.temperature, 0.0f, 1.0f), d = std::clamp(bio.downfall, 0.0f, 1.0f);
        bio.grassColor = parseColor(b, "grass").value_or(grass.sample(t, d, 0x91BD59));
        bio.foliageColor = parseColor(b, "foliage").value_or(foliage.sample(t, d, 0x77AB2F));
        bio.dryFoliageColor = parseColor(b, "dry_foliage").value_or(dry.sample(t, d, 0xA67B4A));
        if (bio.grassModifier == GrassModifier::DarkForest)
            bio.grassColor = ((bio.grassColor & 0xFEFEFE) + 0x28340A) >> 1;
        index_.emplace(bio.id, bio.index);
        biomes_.push_back(std::move(bio));
        if (biomes_.size() >= 255) break;
    }
    if (!index_.contains("minecraft:plains")) {
        if (error) *error = "biomes.json lacks plains";
        return false;
    }
    return true;
}

std::optional<BiomeId> BiomeRegistry::find(std::string_view id) const {
    auto it = index_.find(canonicalId(id));
    if (it == index_.end()) return std::nullopt;
    return it->second;
}

BiomeId BiomeRegistry::require(std::string_view id) const {
    return find(id).value_or(index_.at("minecraft:plains"));
}

BiomeColors BiomeRegistry::colors(BiomeId id, int x, int z) const {
    const Biome& b = biomes_[id];
    BiomeColors c;
    c.grass = b.grassColor;
    c.foliage = b.foliageColor;
    c.dryFoliage = b.dryFoliageColor;
    c.water = b.waterColor;
    if (b.grassModifier == GrassModifier::Swamp)
        c.grass = swampNoise().value(x * 0.0225, z * 0.0225, 0.0) < -0.1 ? 0x4C763C : 0x6A7039;
    return c;
}

} // namespace mc
