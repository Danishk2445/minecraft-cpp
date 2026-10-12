// TagSet.h -- named sets of registry entries (blocks, items) from a folder of tag
// files (data/tags/<kind>/**.json).
//
// The file format is vanilla's ({"values": ["minecraft:stone", "#minecraft:dirt"]})
// extended with '*' globs over ids ("*_planks") and an optional "exclude" list
// using the same entry syntax. Nested folders name nested tags ("mineable/pickaxe.json"
// is minecraft:mineable/pickaxe).
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mc {

using TagId = uint16_t;

// '*' wildcard match against an id without namespace ("oak_stairs" ~ "*_stairs").
bool globMatch(std::string_view pattern, std::string_view text);

class TagSet {
public:
    // Resolves every tag file under `dir` against `count` entries whose ids (without
    // namespace) `idOf` gives. Problems (unknown ids, globs matching nothing, missing
    // references, cycles) are reported in `error`, with `what` naming the entries
    // ("block", "item"); tags still load with whatever resolved.
    bool load(const std::filesystem::path& dir, size_t count, const std::function<std::string_view(size_t)>& idOf,
              std::string_view what, std::string* error);

    size_t size() const { return names_.size(); }
    const std::string& name(TagId t) const { return names_[t]; } // "minecraft:logs"
    // Namespace optional: "logs", "minecraft:logs" and "#minecraft:logs" all work.
    std::optional<TagId> find(std::string_view name) const;
    bool contains(TagId t, size_t entry) const {
        return entry < bits_[t].size() * 64 && ((bits_[t][entry >> 6] >> (entry & 63)) & 1u);
    }
    std::vector<uint16_t> members(TagId t) const; // entry indices

private:
    std::vector<std::string> names_;
    std::unordered_map<std::string, TagId> index_;
    std::vector<std::vector<uint64_t>> bits_; // per tag, a bit per entry
};

} // namespace mc
