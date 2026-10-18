// VisGraph.h -- vanilla VisGraph and VisibilitySet: which faces of a section can see
// each other through its cells that don't block sight (anything but opaque full cubes).
// The renderer's occlusion search (cave culling) only passes through a section from one
// face to another when they do.
#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "core/Math.h"
#include "world/chunk/Chunk.h"

namespace mc {

struct VisibilitySet {
    static constexpr uint64_t kAll = (uint64_t(1) << 36) - 1;
    uint64_t bits = kAll; // bit a*6+b: face a sees face b
    bool visible(Direction a, Direction b) const { return (bits >> (int(a) * 6 + int(b))) & 1u; }
    void set(Direction a, Direction b) {
        bits |= uint64_t(1) << (int(a) * 6 + int(b));
        bits |= uint64_t(1) << (int(b) * 6 + int(a));
    }
    static VisibilitySet none() { return VisibilitySet{0}; }
};

// Vanilla VisGraph.resolve. `opaque` holds the cells (x | z << 4 | y << 8) that block sight.
// Fewer than 256 of them: every face sees every other; all 4096: none do; otherwise flood
// fill from each open cell on the boundary and link the faces each region touches.
VisibilitySet resolveVisibility(const std::bitset<4096>& opaque);

// The visibility of a meshed column's sections, bottom to top.
struct ColumnVisibility {
    std::array<VisibilitySet, kSectionCount> visibility;
};

// Vanilla SectionOcclusionGraph's full update (cave culling): a breadth-first search over
// the sections within the render distance, from the camera's section (or, from outside the
// world's height, the top or bottom layer). A path never turns back along an axis it has
// moved along, leaves a section only by a face that one of the faces it entered by can see,
// and, more than 60 blocks out, only reaches a section whose line toward the camera crosses
// sections already reached. The renderer draws what it reaches (and is in the frustum).
class SectionOcclusionGraph {
public:
    struct Reached {
        const ColumnVisibility* column;
        ChunkPos chunk;
        int index; // section index
    };
    // `columns`: the meshed columns; columns without meshes are reached but not searched
    // through. `renderDistance` in chunks, cylindrical like the renderer's cut-off.
    void search(const glm::dvec3& camera, int renderDistance,
                const std::vector<std::pair<ChunkPos, const ColumnVisibility*>>& columns);
    const std::vector<Reached>& reached() const { return reached_; }

private:
    std::vector<Reached> reached_;
    std::vector<const ColumnVisibility*> columns_; // (2R+1)^2 around the camera's chunk
    std::vector<uint8_t> inView_;
    std::vector<uint16_t> nodes_; // per section: source directions, path directions, visited
    std::vector<uint32_t> queue_;
};

} // namespace mc
