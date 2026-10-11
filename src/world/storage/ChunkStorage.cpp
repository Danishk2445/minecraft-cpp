#include "world/storage/ChunkStorage.h"

#include <format>
#include <fstream>
#include <unordered_map>

#include <zstd.h>

#include "world/biome/Biome.h"
#include "world/block/BlockRegistry.h"

namespace mc {

std::vector<uint8_t> zstdCompress(const std::vector<uint8_t>& data, int level) {
    std::vector<uint8_t> out(ZSTD_compressBound(data.size()));
    const size_t n = ZSTD_compress(out.data(), out.size(), data.data(), data.size(), level);
    if (ZSTD_isError(n)) return {};
    out.resize(n);
    return out;
}

bool zstdDecompress(const std::vector<uint8_t>& data, std::vector<uint8_t>& out) {
    const unsigned long long size = ZSTD_getFrameContentSize(data.data(), data.size());
    if (size == ZSTD_CONTENTSIZE_ERROR || size == ZSTD_CONTENTSIZE_UNKNOWN || size > (256u << 20)) return false;
    out.resize(size_t(size));
    const size_t n = ZSTD_decompress(out.data(), out.size(), data.data(), data.size());
    return !ZSTD_isError(n) && n == out.size();
}

ChunkStorage::ChunkStorage(std::filesystem::path worldDir, const BlockRegistry& blocks, const BiomeRegistry* biomes)
    : dir_(std::move(worldDir)), blocks_(blocks), biomes_(biomes) {}

std::filesystem::path ChunkStorage::pathFor(ChunkPos p) const {
    return dir_ / "region" / std::format("{}.{}", p.x >> 5, p.z >> 5) / std::format("{}.{}.bin", p.x, p.z);
}

bool ChunkStorage::exists(ChunkPos p) const {
    std::error_code ec;
    return std::filesystem::exists(pathFor(p), ec);
}

bool ChunkStorage::save(const Chunk& chunk, const Json& extra, std::string* error) const {
    Json doc = {{"v", 1}, {"pos", {chunk.pos.x, chunk.pos.z}}};
    Json sections = Json::array();
    for (int si = 0; si < kSectionCount; ++si) {
        const Section& s = chunk.sections[size_t(si)];
        Json sec = {{"y", si}};
        if (s.blocks) {
            std::unordered_map<StateId, uint16_t> index;
            Json palette = Json::array();
            std::vector<uint16_t> ids(kSectionVolume);
            for (int i = 0; i < kSectionVolume; ++i) {
                const StateId st = s.blocks->states[size_t(i)];
                auto [it, inserted] = index.emplace(st, uint16_t(index.size()));
                if (inserted) palette.push_back(blocks_.describe(st));
                ids[size_t(i)] = it->second;
            }
            // One byte per block when the palette fits, else two (little endian).
            std::vector<uint8_t> packed;
            const bool wide = palette.size() > 256;
            packed.reserve(ids.size() * (wide ? 2 : 1));
            for (uint16_t v : ids) {
                packed.push_back(uint8_t(v));
                if (wide) packed.push_back(uint8_t(v >> 8));
            }
            sec["palette"] = std::move(palette);
            sec["blocks"] = Json::binary(std::move(packed));
        }
        if (biomes_) {
            std::unordered_map<BiomeId, uint8_t> index;
            Json palette = Json::array();
            std::vector<uint8_t> ids(64);
            for (int i = 0; i < 64; ++i) {
                auto [it, inserted] = index.emplace(s.biomes[size_t(i)], uint8_t(index.size()));
                if (inserted) palette.push_back(biomes_->get(s.biomes[size_t(i)]).id);
                ids[size_t(i)] = it->second;
            }
            sec["biome_palette"] = std::move(palette);
            sec["biomes"] = Json::binary(std::move(ids));
        }
        if (s.blocks || biomes_) sections.push_back(std::move(sec));
    }
    doc["sections"] = std::move(sections);
    if (!extra.is_null()) doc["extra"] = extra;

    const std::vector<uint8_t> bytes = zstdCompress(Json::to_cbor(doc));
    const std::filesystem::path path = pathFor(chunk.pos), tmp = path.string() + ".tmp";
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        if (!f) {
            if (error) *error = "cannot write " + tmp.string();
            return false;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec && error) *error = std::format("cannot replace {}: {}", path.string(), ec.message());
    return !ec;
}

std::unique_ptr<Chunk> ChunkStorage::load(ChunkPos p, Json* extra, std::string* error) const {
    const std::filesystem::path path = pathFor(p);
    std::ifstream f(path, std::ios::binary);
    if (!f) return nullptr;
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::vector<uint8_t> raw;
    if (!zstdDecompress(bytes, raw)) {
        if (error) *error = path.string() + ": not a zstd frame";
        return nullptr;
    }
    Json doc;
    try {
        doc = Json::from_cbor(raw);
    } catch (const Json::exception& e) {
        if (error) *error = std::format("{}: {}", path.string(), e.what());
        return nullptr;
    }
    auto chunk = std::make_unique<Chunk>(p);
    for (const Json& sec : doc.value("sections", Json::array())) {
        const int si = sec.value("y", -1);
        if (si < 0 || si >= kSectionCount) continue;
        Section& s = chunk->sections[size_t(si)];
        if (sec.contains("palette") && sec.contains("blocks")) {
            std::vector<StateId> palette;
            for (const Json& name : sec["palette"]) {
                // Unknown blocks (a removed block or property value) load as air.
                auto st = blocks_.parse(name.get<std::string>());
                palette.push_back(st.value_or(BlockRegistry::kAirState));
            }
            const auto& packed = sec["blocks"].get_binary();
            const bool wide = palette.size() > 256;
            if (packed.size() != size_t(kSectionVolume) * (wide ? 2 : 1)) continue;
            SectionBlocks& b = chunk->mutableBlocks(si);
            for (int i = 0; i < kSectionVolume; ++i) {
                const uint16_t v = wide ? uint16_t(packed[size_t(i) * 2] | (packed[size_t(i) * 2 + 1] << 8)) : packed[size_t(i)];
                const StateId st = v < palette.size() ? palette[v] : BlockRegistry::kAirState;
                b.states[size_t(i)] = st;
                if (st != 0) ++b.nonAir;
            }
        }
        if (biomes_ && sec.contains("biome_palette") && sec.contains("biomes")) {
            std::vector<BiomeId> palette;
            for (const Json& name : sec["biome_palette"]) palette.push_back(biomes_->require(name.get<std::string>()));
            const auto& ids = sec["biomes"].get_binary();
            for (size_t i = 0; i < 64 && i < ids.size(); ++i)
                s.biomes[i] = ids[i] < palette.size() ? palette[ids[i]] : BiomeId(0);
        }
    }
    if (extra) *extra = doc.value("extra", Json());
    return chunk;
}

} // namespace mc
