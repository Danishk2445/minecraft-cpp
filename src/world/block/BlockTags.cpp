#include "world/block/BlockTags.h"

#include "world/block/BlockRegistry.h"

namespace mc {

bool BlockTags::load(const BlockRegistry& registry, const std::filesystem::path& dir, std::string* error) {
    return TagSet::load(
        dir, registry.blockCount(), [&](size_t i) -> std::string_view { return registry.block(i).id; }, "block", error);
}

} // namespace mc
