#include "world/chunk/Chunk.h"

namespace mc {

SectionBlocks& Chunk::mutableBlocks(int si) {
    auto& ptr = sections[size_t(si)].blocks;
    if (!ptr) ptr = std::make_shared<SectionBlocks>();
    else if (ptr.use_count() > 1) ptr = std::make_shared<SectionBlocks>(*ptr);
    return *ptr;
}

SectionLight& Chunk::mutableLight(int si) {
    Section& sec = sections[size_t(si)];
    auto& ptr = sec.light;
    if (!ptr) {
        ptr = std::make_shared<SectionLight>();
        ptr->values.fill(sec.uniformLight);
    } else if (ptr.use_count() > 1) {
        ptr = std::make_shared<SectionLight>(*ptr);
    }
    return *ptr;
}

StateId Chunk::set(int lx, int y, int lz, StateId state) {
    if (y < kWorldMinY || y > kWorldMaxY) return 0;
    const int si = sectionIndexOfY(y);
    Section& sec = sections[size_t(si)];
    const int idx = SectionBlocks::index(lx, (y - kWorldMinY) & 15, lz);
    const StateId old = sec.blocks ? sec.blocks->states[size_t(idx)] : StateId(0);
    if (old == state) return old;
    if (!sec.blocks && state == 0) return old;
    SectionBlocks& b = mutableBlocks(si);
    b.states[size_t(idx)] = state;
    if (old == 0) ++b.nonAir;
    if (state == 0) --b.nonAir;
    if (b.nonAir == 0) sec.blocks.reset();
    return old;
}

void Chunk::setLight(int lx, int y, int lz, uint8_t packed) {
    if (y < kWorldMinY || y > kWorldMaxY) return;
    const int si = sectionIndexOfY(y);
    const int idx = SectionBlocks::index(lx, (y - kWorldMinY) & 15, lz);
    if (!sections[size_t(si)].light && sections[size_t(si)].uniformLight == packed) return;
    mutableLight(si).values[size_t(idx)] = packed;
}

int Chunk::topY() const {
    for (int si = kSectionCount - 1; si >= 0; --si) {
        const auto& b = sections[size_t(si)].blocks;
        if (!b) continue;
        for (int y = 15; y >= 0; --y)
            for (int i = 0; i < 256; ++i)
                if (b->states[size_t((y << 8) | i)] != 0) return sectionMinY(si) + y + 1;
    }
    return kWorldMinY;
}

bool Chunk::sameStorage(const Chunk& other) const {
    for (int i = 0; i < kSectionCount; ++i)
        if (sections[size_t(i)].blocks != other.sections[size_t(i)].blocks) return false;
    return true;
}

} // namespace mc
