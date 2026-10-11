#include "world/light/LightEngine.h"

#include <algorithm>
#include <climits>
#include <cstring>

#include "world/chunk/ChunkManager.h"

namespace mc {
namespace {

// Region for computeChunk: the 3x3 chunk neighborhood, full world height.
constexpr int kR = 48;
constexpr int kH = kWorldHeight;
constexpr int kLayer = kR * kR;
constexpr size_t kCells = size_t(kLayer) * kH;

constexpr int kOffsets[6] = {-kLayer, kLayer, -kR, kR, -1, 1}; // D U N S W E

constexpr uint8_t bit(int d) { return uint8_t(1u << d); }

struct Scratch {
    std::vector<uint8_t> opacity = std::vector<uint8_t>(kCells);
    std::vector<uint8_t> faces = std::vector<uint8_t>(kCells);
    std::vector<uint8_t> sky = std::vector<uint8_t>(kCells);
    std::vector<uint8_t> block = std::vector<uint8_t>(kCells);
    std::vector<uint32_t> queue;
    std::array<int, kR * kR> runBottom{};
};

Scratch& scratch() {
    thread_local Scratch s;
    return s;
}

// Breadth-first propagation over the region from the queued cells.
void flood(Scratch& s, std::vector<uint8_t>& light, bool skyMode) {
    for (size_t head = 0; head < s.queue.size(); ++head) {
        const uint32_t i = s.queue[head];
        const int level = light[i];
        if (level <= 1) continue;
        const int ix = int(i % kR), iz = int((i / kR) % kR), iy = int(i / kLayer);
        const bool inBounds[6] = {iy > 0, iy < kH - 1, iz > 0, iz < kR - 1, ix > 0, ix < kR - 1};
        const uint8_t myFaces = s.faces[i];
        for (int d = 0; d < 6; ++d) {
            if (!inBounds[d] || (myFaces & bit(d))) continue;
            const uint32_t ni = uint32_t(int(i) + kOffsets[d]);
            if (s.faces[ni] & bit(d ^ 1)) continue;
            const int op = s.opacity[ni];
            const int nl = (skyMode && d == 0 && level == 15 && op == 0) ? 15 : level - std::max(1, op);
            if (nl > light[ni]) {
                light[ni] = uint8_t(nl);
                s.queue.push_back(ni);
            }
        }
    }
    s.queue.clear();
}

} // namespace

LightEngine::LightEngine(const BlockRegistry& registry) {
    const size_t n = registry.stateCount();
    opacity_.resize(n);
    emission_.resize(n);
    faces_.resize(n);
    for (size_t s = 0; s < n; ++s) {
        const StateInfo& info = registry.info(StateId(s));
        opacity_[s] = info.opacity;
        emission_[s] = info.emission;
        faces_[s] = info.opacity >= 15 ? uint8_t(0) : info.lightBlockingFaces;
    }
}

void LightEngine::computeChunk(const Neighborhood& n, Chunk& out) const {
    Scratch& s = scratch();
    std::fill(s.sky.begin(), s.sky.end(), uint8_t(0));
    std::fill(s.block.begin(), s.block.end(), uint8_t(0));

    // Gather opacity, blocking faces and emitters.
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) {
            const ColumnSnapshot* col = n.column(dx, dz);
            for (int si = 0; si < kSectionCount; ++si) {
                const SectionBlocks* blocks = col ? col->blocks[size_t(si)].get() : nullptr;
                for (int y = 0; y < 16; ++y)
                    for (int z = 0; z < 16; ++z) {
                        const size_t row = (size_t(si * 16 + y) * kR + size_t((dz + 1) * 16 + z)) * kR + size_t((dx + 1) * 16);
                        if (!blocks) {
                            std::memset(&s.opacity[row], 0, 16);
                            std::memset(&s.faces[row], 0, 16);
                            continue;
                        }
                        const StateId* states = &blocks->states[size_t((y << 8) | (z << 4))];
                        for (int x = 0; x < 16; ++x) {
                            const StateId st = states[x];
                            s.opacity[row + size_t(x)] = opacity_[st];
                            s.faces[row + size_t(x)] = faces_[st];
                            if (const uint8_t em = emission_[st]) {
                                s.block[row + size_t(x)] = em;
                                s.queue.push_back(uint32_t(row + size_t(x)));
                            }
                        }
                    }
            }
        }
    flood(s, s.block, false);

    // Skylight: straight down from the top through cells that let it pass unchanged.
    for (int iz = 0; iz < kR; ++iz)
        for (int ix = 0; ix < kR; ++ix) {
            int iy = kH - 1;
            for (; iy >= 0; --iy) {
                const size_t i = (size_t(iy) * kR + size_t(iz)) * kR + size_t(ix);
                if (s.opacity[i] != 0 || (s.faces[i] & bit(1))) break;
                if (iy < kH - 1 && (s.faces[i + kLayer] & bit(0))) break;
                s.sky[i] = 15;
            }
            s.runBottom[size_t(iz * kR + ix)] = iy + 1;
        }
    // Seeds: the bottom of each run (it may continue, dimmer, through water or leaves),
    // and run cells beside a column that is darker at that height.
    for (int iz = 0; iz < kR; ++iz)
        for (int ix = 0; ix < kR; ++ix) {
            const int bottom = s.runBottom[size_t(iz * kR + ix)];
            if (bottom >= kH) continue;
            int top = bottom + 1;
            if (ix > 0) top = std::max(top, s.runBottom[size_t(iz * kR + ix - 1)]);
            if (ix < kR - 1) top = std::max(top, s.runBottom[size_t(iz * kR + ix + 1)]);
            if (iz > 0) top = std::max(top, s.runBottom[size_t((iz - 1) * kR + ix)]);
            if (iz < kR - 1) top = std::max(top, s.runBottom[size_t((iz + 1) * kR + ix)]);
            top = std::min(top, kH);
            for (int iy = bottom; iy < top; ++iy) s.queue.push_back(uint32_t((size_t(iy) * kR + size_t(iz)) * kR + size_t(ix)));
        }
    flood(s, s.sky, true);

    // Store the center chunk's light, collapsing uniform sections.
    for (int si = 0; si < kSectionCount; ++si) {
        Section& sec = out.sections[size_t(si)];
        auto packedAt = [&](int x, int y, int z) {
            const size_t i = (size_t(si * 16 + y) * kR + size_t(16 + z)) * kR + size_t(16 + x);
            return packLight(s.sky[i], s.block[i]);
        };
        const uint8_t first = packedAt(0, 0, 0);
        bool uniform = true;
        for (int y = 0; y < 16 && uniform; ++y)
            for (int z = 0; z < 16 && uniform; ++z)
                for (int x = 0; x < 16; ++x)
                    if (packedAt(x, y, z) != first) {
                        uniform = false;
                        break;
                    }
        if (uniform) {
            sec.light.reset();
            sec.uniformLight = first;
            continue;
        }
        auto light = std::make_shared<SectionLight>();
        for (int y = 0; y < 16; ++y)
            for (int z = 0; z < 16; ++z)
                for (int x = 0; x < 16; ++x) light->values[size_t(SectionBlocks::index(x, y, z))] = packedAt(x, y, z);
        sec.light = std::move(light);
    }
}

namespace {

// Live-world access for incremental updates (main thread).
class LiveAccess {
public:
    explicit LiveAccess(ChunkManager& cm) : cm_(cm) {}

    Chunk* chunk(const BlockPos& p) {
        const ChunkPos cp = ChunkPos::of(p);
        if (cp != lastPos_) {
            lastPos_ = cp;
            last_ = cm_.stage(cp) >= ChunkStage::Lit ? cm_.chunk(cp) : nullptr;
        }
        return last_;
    }
    StateId state(Chunk* c, const BlockPos& p) { return c->get(p.x & 15, p.y, p.z & 15); }
    int level(Chunk* c, const BlockPos& p, bool sky) {
        const uint8_t v = c->lightAt(p.x & 15, p.y, p.z & 15);
        return sky ? v >> 4 : v & 15;
    }
    void setLevel(Chunk* c, const BlockPos& p, bool sky, int value) {
        if (p.y < kWorldMinY || p.y > kWorldMaxY) return;
        const uint8_t v = c->lightAt(p.x & 15, p.y, p.z & 15);
        const uint8_t nv = sky ? uint8_t((value << 4) | (v & 15)) : uint8_t((v & 0xF0) | value);
        if (nv != v) c->setLight(p.x & 15, p.y, p.z & 15, nv);
    }

private:
    ChunkManager& cm_;
    ChunkPos lastPos_{INT_MIN, INT_MIN};
    Chunk* last_ = nullptr;
};

} // namespace

void LightEngine::update(ChunkManager& cm, const BlockPos& p, std::vector<BlockPos>& changed) const {
    LiveAccess acc(cm);
    Chunk* origin = acc.chunk(p);
    if (!origin || p.y < kWorldMinY || p.y > kWorldMaxY) return;

    struct Node {
        BlockPos pos;
        int level;
    };
    std::vector<Node> removeQueue;
    std::vector<BlockPos> addQueue;
    std::vector<BlockPos> emitters;

    for (int channel = 0; channel < 2; ++channel) {
        const bool sky = channel == 0;
        removeQueue.clear();
        addQueue.clear();

        const int old = acc.level(origin, p, sky);
        if (old > 0) {
            acc.setLevel(origin, p, sky, 0);
            removeQueue.push_back({p, old});
            changed.push_back(p);
        }
        // Removal is purely level-based (no face checks): the edited block's faces
        // changed, so they cannot tell what light passed through before. Removing too
        // much is safe because every boundary cell is re-added below.
        for (size_t head = 0; head < removeQueue.size(); ++head) {
            const Node q = removeQueue[head];
            for (int d = 0; d < 6; ++d) {
                const BlockPos np = q.pos.offset(Direction(d));
                Chunk* nc = acc.chunk(np);
                if (!nc || np.y < kWorldMinY - 1 || np.y > kWorldMaxY + 1) continue;
                const int ln = acc.level(nc, np, sky);
                if (ln == 0) continue;
                const bool inWorld = np.y >= kWorldMinY && np.y <= kWorldMaxY;
                const bool dependent = inWorld && (ln < q.level || (sky && d == 0 && q.level == 15 && ln == 15));
                if (dependent) {
                    acc.setLevel(nc, np, sky, 0);
                    removeQueue.push_back({np, ln});
                    changed.push_back(np);
                    // An emitter swept up by the removal stays a source of its own light.
                    if (!sky && emission_[acc.state(nc, np)] > 0) emitters.push_back(np);
                } else {
                    addQueue.push_back(np);
                }
            }
        }
        for (const BlockPos& e : emitters) {
            Chunk* ec = acc.chunk(e);
            const int em = emission_[acc.state(ec, e)];
            if (em > acc.level(ec, e, false)) acc.setLevel(ec, e, false, em);
            addQueue.push_back(e);
        }
        emitters.clear();

        if (!sky) {
            const int em = emission_[acc.state(origin, p)];
            if (em > acc.level(origin, p, false)) {
                acc.setLevel(origin, p, false, em);
                changed.push_back(p);
            }
        }
        addQueue.push_back(p);
        for (int d = 0; d < 6; ++d) addQueue.push_back(p.offset(Direction(d)));

        for (size_t head = 0; head < addQueue.size(); ++head) {
            const BlockPos a = addQueue[head];
            Chunk* ac = acc.chunk(a);
            if (!ac) continue;
            const int la = acc.level(ac, a, sky);
            if (la <= 1) continue;
            const bool aInWorld = a.y >= kWorldMinY && a.y <= kWorldMaxY;
            const uint8_t aFaces = aInWorld ? faces_[acc.state(ac, a)] : uint8_t(0);
            for (int d = 0; d < 6; ++d) {
                if (aFaces & bit(d)) continue;
                const BlockPos np = a.offset(Direction(d));
                if (np.y < kWorldMinY || np.y > kWorldMaxY) continue;
                Chunk* nc = acc.chunk(np);
                if (!nc) continue;
                const StateId ns = acc.state(nc, np);
                if (faces_[ns] & bit(d ^ 1)) continue;
                const int op = opacity_[ns];
                const int nl = (sky && d == 0 && la == 15 && op == 0) ? 15 : la - std::max(1, op);
                if (nl > acc.level(nc, np, sky)) {
                    acc.setLevel(nc, np, sky, nl);
                    addQueue.push_back(np);
                    changed.push_back(np);
                }
            }
        }
    }
}

} // namespace mc
