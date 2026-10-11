// BlockTags.h -- named block sets from data/tags/blocks/**.json.
//
// The file format is vanilla's ({"values": ["minecraft:stone", "#minecraft:dirt"]})
// extended with '*' globs over block ids ("*_planks") and an optional "exclude"
// list using the same entry syntax. Nested folders name nested tags
// ("mineable/pickaxe.json" is minecraft:mineable/pickaxe).
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mc {

class BlockRegistry;

using TagId = uint16_t;

class BlockTags {
public:
    // Resolves every tag file under `dir` against the registry. Problems (unknown
    // ids, globs matching nothing, missing references, cycles) are reported in
    // `error`; tags still load with whatever resolved.
    bool load(const BlockRegistry& registry, const std::filesystem::path& dir, std::string* error);

    size_t size() const { return names_.size(); }
    const std::string& name(TagId t) const { return names_[t]; } // "minecraft:logs"
    // Namespace optional: "logs", "minecraft:logs" and "#minecraft:logs" all work.
    std::optional<TagId> find(std::string_view name) const;
    bool containsBlock(TagId t, size_t blockIndex) const {
        return (bits_[t][blockIndex >> 6] >> (blockIndex & 63)) & 1u;
    }
    std::vector<uint16_t> blocks(TagId t) const; // block indices

private:
    std::vector<std::string> names_;
    std::unordered_map<std::string, TagId> index_;
    std::vector<std::vector<uint64_t>> bits_; // per tag, a bit per block index
};

} // namespace mc
