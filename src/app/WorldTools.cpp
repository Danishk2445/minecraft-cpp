#include "app/WorldTools.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <map>
#include <memory>

#include "core/Image.h"
#include "core/JobSystem.h"
#include "core/Log.h"
#include "core/Timer.h"
#include "resources/Atlas.h"
#include "resources/BlockModels.h"
#include "resources/ModelBaker.h"
#include "resources/ResourcePack.h"
#include "world/biome/Biome.h"
#include "world/block/BlockRegistry.h"
#include "world/chunk/ChunkManager.h"
#include "world/gen/OverworldGenerator.h"
#include "world/light/LightEngine.h"

namespace mc {
namespace {

// Pack, block registry and biomes, built without a GL context.
struct Context {
    std::unique_ptr<ResourcePack> pack;
    std::optional<Atlas> atlas;
    std::unique_ptr<ModelStore> store;
    std::unique_ptr<ModelBaker> baker;
    BlockModels models;
    BlockRegistry registry;
    BiomeRegistry biomes;

    bool load(const CliOptions& o) {
        pack = std::make_unique<ResourcePack>(o.assetsDir);
        atlas = buildAtlas(*pack, "blocks", 0);
        if (!atlas) {
            log::error("Could not build the block atlas from {}", o.assetsDir.string());
            return false;
        }
        store = std::make_unique<ModelStore>(*pack);
        baker = std::make_unique<ModelBaker>(*store, *atlas);
        models.load(*pack, *store, *baker);
        std::string err;
        if (!registry.build(models, *atlas, o.dataDir, &err)) log::warn("block registry: {}", err);
        if (!biomes.load(o.dataDir, o.assetsDir, &err)) {
            log::error("biomes: {}", err);
            return false;
        }
        return true;
    }
};

// Map colors in the style of common seed-map tools; unknown biomes get a hashed color.
uint32_t mapColor(std::string_view id) {
    static const std::map<std::string_view, uint32_t, std::less<>> colors = {
        {"ocean", 0x000070}, {"deep_ocean", 0x000030}, {"warm_ocean", 0x0000AC}, {"lukewarm_ocean", 0x000090},
        {"deep_lukewarm_ocean", 0x000040}, {"cold_ocean", 0x202070}, {"deep_cold_ocean", 0x202038},
        {"frozen_ocean", 0x7070D6}, {"deep_frozen_ocean", 0x404090}, {"river", 0x0000FF}, {"frozen_river", 0xA0A0FF},
        {"beach", 0xFADE55}, {"snowy_beach", 0xFAF0C0}, {"stony_shore", 0xA2A284}, {"plains", 0x8DB360},
        {"sunflower_plains", 0xB5DB88}, {"snowy_plains", 0xFFFFFF}, {"ice_spikes", 0xB4DCDC}, {"desert", 0xFA9418},
        {"swamp", 0x07F9B2}, {"forest", 0x056621}, {"flower_forest", 0x2D8E49}, {"birch_forest", 0x307444},
        {"old_growth_birch_forest", 0x589C6C}, {"dark_forest", 0x40511A}, {"taiga", 0x0B6659},
        {"snowy_taiga", 0x31554A}, {"old_growth_pine_taiga", 0x596651}, {"old_growth_spruce_taiga", 0x818E79},
        {"savanna", 0xBDB25F}, {"savanna_plateau", 0xA79D64}, {"windswept_savanna", 0xE5DA87}, {"jungle", 0x537B09},
        {"sparse_jungle", 0x628B17}, {"bamboo_jungle", 0x768E14}, {"badlands", 0xD94515},
        {"wooded_badlands", 0xB09765}, {"eroded_badlands", 0xFF6D3D}, {"windswept_hills", 0x606060},
        {"windswept_forest", 0x454F3E}, {"windswept_gravelly_hills", 0x888888}, {"meadow", 0x60A445},
        {"grove", 0x47726C}, {"snowy_slopes", 0xC4C4C4}, {"frozen_peaks", 0xA0A0A0}, {"jagged_peaks", 0xDCDCC8},
        {"stony_peaks", 0x7B8F74}, {"cherry_grove", 0xFF91C8}, {"mushroom_fields", 0xFF00FF},
        {"lush_caves", 0x7BA331}, {"dripstone_caves", 0x86604B},
    };
    if (auto it = colors.find(id); it != colors.end()) return it->second;
    uint32_t h = 2166136261u;
    for (char c : id) h = (h ^ uint8_t(c)) * 16777619u;
    return h & 0xFFFFFF;
}

int dumpColumn(const Context& ctx, const CliOptions& o) {
    const OverworldGenerator gen(ctx.registry, ctx.biomes, o.seed);
    std::printf("%s", gen.describeColumn(o.dumpColumn->x, o.dumpColumn->y).c_str());
    std::printf("terrain top %d\n", gen.terrainTop(o.dumpColumn->x, o.dumpColumn->y));
    const glm::dvec3 sp = gen.spawnPoint();
    std::printf("spawn %.1f %.1f %.1f\n", sp.x, sp.y, sp.z);
    return 0;
}

// Top-down map of surface biomes, shaded by the spline height estimate, plus
// biome statistics over the same samples.
int biomeMap(const Context& ctx, const CliOptions& o) {
    const OverworldGenerator gen(ctx.registry, ctx.biomes, o.seed);
    const int step = std::max(1, o.mapStep);
    const int n = std::max(1, o.mapSize / step);
    const glm::ivec2 center = o.pos ? glm::ivec2(int(o.pos->x), int(o.pos->z)) : glm::ivec2(0);
    std::vector<int> height(size_t(n) * size_t(n));
    std::vector<BiomeId> biome(height.size());
    Stopwatch sw;
    JobSystem jobs(o.threads);
    // One job per row; rows write disjoint slices.
    for (int row = 0; row < n; ++row)
        jobs.submit(0, [&, row] {
            for (int col = 0; col < n; ++col) {
                const int x = center.x + (col - n / 2) * step, z = center.y + (row - n / 2) * step;
                const int h = gen.estimatedSurface(x, z);
                height[size_t(row) * size_t(n) + size_t(col)] = h;
                biome[size_t(row) * size_t(n) + size_t(col)] = gen.biomeAt(x, h, z);
            }
        });
    jobs.waitIdle();

    Image img(n, n);
    std::vector<size_t> counts(ctx.biomes.size());
    std::vector<glm::ivec3> nearest(ctx.biomes.size(), glm::ivec3(0, 0, -1)); // x, z, squared distance
    size_t ocean = 0;
    for (int row = 0; row < n; ++row)
        for (int col = 0; col < n; ++col) {
            const size_t i = size_t(row) * size_t(n) + size_t(col);
            const std::string_view id = shortId(ctx.biomes.get(biome[i]).id);
            ++counts[biome[i]];
            {
                const int dx = (col - n / 2) * step, dz = (row - n / 2) * step;
                const int d2 = dx * dx + dz * dz;
                glm::ivec3& best = nearest[biome[i]];
                if (best.z < 0 || d2 < best.z) best = {center.x + dx, center.y + dz, d2};
            }
            if (id.find("ocean") != std::string_view::npos) ++ocean;
            const uint32_t c = mapColor(id);
            // Hillshade from the north-west neighbor; brighter with altitude.
            const int hw = height[size_t(row) * size_t(n) + size_t(std::max(col - 1, 0))];
            const int hn = height[size_t(std::max(row - 1, 0)) * size_t(n) + size_t(col)];
            const int h = height[i];
            const double slope = double((h - hw) + (h - hn)) / double(step);
            const double alt = std::clamp((std::max(h, OverworldGenerator::kSeaLevel) - 40) / 200.0, 0.0, 1.0);
            const double shade = std::clamp(0.8 + 0.3 * alt + 0.25 * slope, 0.35, 1.3);
            uint8_t* p = img.px(col, row);
            p[0] = uint8_t(std::clamp(((c >> 16) & 255) * shade, 0.0, 255.0));
            p[1] = uint8_t(std::clamp(((c >> 8) & 255) * shade, 0.0, 255.0));
            p[2] = uint8_t(std::clamp((c & 255) * shade, 0.0, 255.0));
            p[3] = 255;
        }
    const size_t total = height.size();
    std::vector<std::pair<size_t, BiomeId>> order;
    for (size_t b = 0; b < counts.size(); ++b)
        if (counts[b]) order.emplace_back(counts[b], BiomeId(b));
    std::sort(order.rbegin(), order.rend());
    std::printf("seed %lld, %dx%d blocks around %d,%d (step %d) in %.0f ms\n", o.seed, n * step, n * step, center.x,
                center.y, step, sw.millis());
    for (const auto& [count, b] : order)
        std::printf("  %-26s %6.2f%%  nearest %d,%d\n", std::string(shortId(ctx.biomes.get(b).id)).c_str(),
                    100.0 * double(count) / double(total), nearest[b].x, nearest[b].y);
    const size_t hi = size_t(std::max_element(height.begin(), height.end()) - height.begin());
    std::printf("biomes %zu, ocean %.1f%%, highest estimate y %d at %d,%d\n", order.size(),
                100.0 * double(ocean) / double(total), height[hi], center.x + (int(hi % size_t(n)) - n / 2) * step,
                center.y + (int(hi / size_t(n)) - n / 2) * step);
    if (!img.savePng(o.biomeMap)) {
        log::error("Could not write {}", o.biomeMap.string());
        return 1;
    }
    return 0;
}

// Vertical cross-section of TERRAIN-stage chunks: x across, y up (2 px per block).
int slice(const Context& ctx, const CliOptions& o) {
    const OverworldGenerator gen(ctx.registry, ctx.biomes, o.seed);
    const glm::dvec3 at = o.pos.value_or(gen.spawnPoint());
    const int z = int(std::floor(at.z)), width = std::min(o.mapSize, 4096);
    const int x0 = int(std::floor(at.x)) - width / 2;
    std::map<std::string_view, uint32_t, std::less<>> palette = {
        {"stone", 0x7F7F7F}, {"deepslate", 0x4D4D52}, {"water", 0x3F76E4}, {"lava", 0xFF7A00}, {"bedrock", 0x222222},
        {"grass_block", 0x5FA040}, {"dirt", 0x86603F}, {"coarse_dirt", 0x77553A}, {"podzol", 0x5A3F1C},
        {"mycelium", 0x6F6265}, {"sand", 0xDBCFA3}, {"red_sand", 0xBE6621}, {"sandstone", 0xD8CB9B},
        {"red_sandstone", 0xB5621F}, {"gravel", 0x8A8280}, {"clay", 0xA0A6B3}, {"snow_block", 0xF5FBFB},
        {"packed_ice", 0x8DB4FA}, {"calcite", 0xDFE0DC}, {"terracotta", 0x985E43}, {"mud", 0x3C393D},
    };
    Image img(width * 2, kWorldHeight * 2, 0x000000FFu);
    std::vector<std::unique_ptr<Chunk>> chunks;
    for (int cx = x0 >> 4; cx <= (x0 + width - 1) >> 4; ++cx) {
        auto c = std::make_unique<Chunk>(ChunkPos(cx, z >> 4));
        gen.generateTerrain(*c);
        chunks.push_back(std::move(c));
    }
    size_t underground = 0, caveAir = 0;
    for (const auto& c : chunks)
        for (int lz = 0; lz < 16; ++lz)
            for (int lx = 0; lx < 16; ++lx)
                for (int y = -56; y <= 40; ++y) {
                    ++underground;
                    if (c->get(lx, y, lz) == 0) ++caveAir;
                }
    for (int i = 0; i < width; ++i) {
        const int x = x0 + i;
        const Chunk& c = *chunks[size_t((x >> 4) - (x0 >> 4))];
        for (int y = kWorldMinY; y <= kWorldMaxY; ++y) {
            const StateId s = c.get(x & 15, y, z & 15);
            uint32_t rgb = 0xA8C8FF; // air above ground: sky
            if (s != 0) {
                const std::string_view id = shortId(ctx.registry.blockOf(s).id);
                auto it = palette.find(id);
                if (it != palette.end()) {
                    rgb = it->second;
                } else {
                    uint32_t h = 2166136261u;
                    for (char ch : id) h = (h ^ uint8_t(ch)) * 16777619u;
                    rgb = h & 0xFFFFFF;
                }
            } else if (y < kWorldMaxY) {
                // Air with something solid above it is cave air.
                for (int yy = y + 1; yy <= std::min(kWorldMaxY, y + 200); ++yy)
                    if (c.get(x & 15, yy, z & 15) != 0) {
                        rgb = 0x101010;
                        break;
                    }
            }
            for (int py = 0; py < 2; ++py)
                for (int px = 0; px < 2; ++px) {
                    uint8_t* p = img.px(i * 2 + px, (kWorldMaxY - y) * 2 + py);
                    p[0] = uint8_t(rgb >> 16), p[1] = uint8_t(rgb >> 8), p[2] = uint8_t(rgb), p[3] = 255;
                }
        }
    }
    // Sea level and y=0 guides.
    for (int px = 0; px < img.width; px += 4)
        for (int y : {OverworldGenerator::kSeaLevel, 0}) {
            uint8_t* p = img.px(px, (kWorldMaxY - y) * 2);
            p[0] = 255, p[1] = 0, p[2] = 0;
        }
    std::printf("slice z=%d, x %d..%d; air between y -56 and 40: %.1f%%\n", z, x0, x0 + width - 1,
                100.0 * double(caveAir) / double(underground));
    return img.savePng(o.slice) ? 0 : 1;
}

// Generates and lights a square of chunks through the real pipeline.
int benchGen(const Context& ctx, const CliOptions& o) {
    auto gen = std::make_shared<OverworldGenerator>(ctx.registry, ctx.biomes, o.seed);
    JobSystem jobs(o.threads);
    // The view of radius r needs lit chunks out to r + 1: pick r so about N chunks get lit.
    const int r = std::max(0, int(std::ceil((std::sqrt(double(o.benchGen)) - 1.0) / 2.0)) - 1);
    const ChunkPos center = o.pos ? ChunkPos::of(BlockPos{int(o.pos->x), 0, int(o.pos->z)}) : ChunkPos{0, 0};
    ChunkManager cm(ctx.registry, gen, jobs);
    cm.setLightEngine(std::make_shared<LightEngine>(ctx.registry));
    Stopwatch sw;
    cm.setView(center, r);
    while (!cm.viewReady()) {
        cm.update();
        jobs.drainMain(1.0);
        if (cm.stats().inFlight > 0 && jobs.workerCount() > 0) std::this_thread::yield();
    }
    const double s = sw.seconds();
    const ChunkManager::Stats st = cm.stats(); // per-stage counts: accumulate the later stages
    const int lit = st.lit, final = st.final + lit, generated = st.terrain + st.decorated + final;
    std::printf("%d chunks generated, %d block-final, %d lit in %.2f s with %u worker(s): %.0f generated/s, %.0f lit/s\n",
                generated, final, lit, s, jobs.workerCount(), generated / s, lit / s);
    return 0;
}

} // namespace

std::optional<int> runWorldTools(const CliOptions& o) {
    if (!o.dumpColumn && o.biomeMap.empty() && o.slice.empty() && o.benchGen <= 0) return std::nullopt;
    Stopwatch sw;
    Context ctx;
    if (!ctx.load(o)) return 1;
    log::info("World tool context loaded in {:.0f} ms", sw.millis());
    if (o.dumpColumn) return dumpColumn(ctx, o);
    if (!o.biomeMap.empty()) return biomeMap(ctx, o);
    if (!o.slice.empty()) return slice(ctx, o);
    return benchGen(ctx, o);
}

} // namespace mc
