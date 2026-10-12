// BlockTags.h -- named block sets from data/tags/blocks/**.json (core/TagSet's format).
#pragma once

#include "core/TagSet.h"

namespace mc {

class BlockRegistry;

class BlockTags : public TagSet {
public:
    bool load(const BlockRegistry& registry, const std::filesystem::path& dir, std::string* error);
    bool containsBlock(TagId t, size_t blockIndex) const { return contains(t, blockIndex); }
    std::vector<uint16_t> blocks(TagId t) const { return members(t); } // block indices
};

} // namespace mc
