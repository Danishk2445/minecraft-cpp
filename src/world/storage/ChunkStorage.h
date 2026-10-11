// ChunkStorage.h -- one zstd-compressed CBOR file per edited chunk at
// <world>/region/<rx>.<rz>/<cx>.<cz>.bin (rx = cx >> 5). Blocks and biomes are
// stored as palettes of names, so saves survive changes to state numbering.
// Thread-safe: loads and saves run on worker jobs.
#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/Json.h"
#include "world/chunk/Chunk.h"

namespace mc {

class BiomeRegistry;

class ChunkStorage {
public:
    ChunkStorage(std::filesystem::path worldDir, const BlockRegistry& blocks, const BiomeRegistry* biomes);

    bool exists(ChunkPos p) const;
    // The saved chunk and its extra data (scheduled ticks, ...), or null if absent/corrupt.
    std::unique_ptr<Chunk> load(ChunkPos p, Json* extra, std::string* error = nullptr) const;
    // Writes atomically (temp file, then rename).
    bool save(const Chunk& chunk, const Json& extra, std::string* error = nullptr) const;

    std::filesystem::path pathFor(ChunkPos p) const;
    const std::filesystem::path& worldDir() const { return dir_; }

private:
    std::filesystem::path dir_;
    const BlockRegistry& blocks_;
    const BiomeRegistry* biomes_;
};

// zstd helpers (also used for other world files).
std::vector<uint8_t> zstdCompress(const std::vector<uint8_t>& data, int level = 3);
bool zstdDecompress(const std::vector<uint8_t>& data, std::vector<uint8_t>& out);

} // namespace mc
