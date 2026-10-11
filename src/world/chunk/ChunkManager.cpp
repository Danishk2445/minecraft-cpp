#include "world/chunk/ChunkManager.h"

#include <algorithm>

#include "core/Log.h"
#include "world/block/BlockGetter.h"
#include "world/block/BlockRegistry.h"
#include "world/block/BlockRules.h"
#include "world/light/LightEngine.h"
#include "world/storage/ChunkStorage.h"

#include <unordered_set>

namespace mc {

ChunkManager::ChunkManager(const BlockRegistry& registry, std::shared_ptr<const ChunkGenerator> generator,
                           JobSystem& jobs)
    : registry_(registry), generator_(std::move(generator)), jobs_(jobs) {}

ChunkManager::~ChunkManager() = default; // destroying alive_ turns pending callbacks into no-ops

void ChunkManager::setLightEngine(std::shared_ptr<const LightEngine> engine) {
    lightEngine_ = engine;
    if (engine) lightFn_ = [engine](const Neighborhood& n, Chunk& out) { engine->computeChunk(n, out); };
    else lightFn_ = nullptr;
}

ChunkManager::Entry* ChunkManager::find(ChunkPos p) {
    auto it = entries_.find(p);
    return it == entries_.end() ? nullptr : it->second.get();
}

const ChunkManager::Entry* ChunkManager::find(ChunkPos p) const {
    auto it = entries_.find(p);
    return it == entries_.end() ? nullptr : it->second.get();
}

void ChunkManager::setView(ChunkPos center, int renderDistance) {
    if (center == center_ && renderDistance == renderDistance_ && !entries_.empty()) return;
    center_ = center;
    renderDistance_ = std::max(renderDistance, 1);
    viewChanged_ = true;
}

template <class Work, class Done>
void ChunkManager::submit(Entry& e, int priority, Work work, Done done) {
    e.busy = true;
    ++inFlight_;
    std::weak_ptr<int> token = alive_;
    JobSystem* jobs = &jobs_;
    const ChunkPos pos = e.pos;
    const uint64_t id = e.id;
    jobs_.submit(priority, [this, token, jobs, pos, id, work = std::move(work), done = std::move(done)]() mutable {
        auto result = work();
        jobs->postMain([this, token, pos, id, result = std::move(result), done = std::move(done)]() mutable {
            if (token.expired()) return; // the manager is gone
            --inFlight_;
            Entry* entry = find(pos);
            if (!entry || entry->id != id) return; // unloaded (and maybe reloaded) meanwhile
            entry->busy = false;
            done(*entry, std::move(result));
        });
    });
}

bool ChunkManager::neighborsAtLeast(const Entry& e, ChunkStage s) const {
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dz == 0) continue;
            const Entry* n = find({e.pos.x + dx, e.pos.z + dz});
            if (!n || n->stage < s) return false;
        }
    return true;
}

void ChunkManager::submitTerrain(Entry& e) {
    auto gen = generator_;
    const ChunkPos pos = e.pos;
    submit(
        e, e.dist * 4 + 4,
        [gen, pos] {
            auto c = std::make_shared<Chunk>(pos);
            gen->generateTerrain(*c);
            return std::shared_ptr<const Chunk>(std::move(c));
        },
        [](Entry& en, std::shared_ptr<const Chunk> c) {
            en.terrain = std::move(c);
            en.stage = ChunkStage::Terrain;
        });
}

void ChunkManager::submitDecorate(Entry& e) {
    TerrainNeighborhood n;
    n.center = e.pos;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) n.chunks[size_t((dz + 1) * 3 + dx + 1)] = find({e.pos.x + dx, e.pos.z + dz})->terrain;
    auto gen = generator_;
    submit(
        e, e.dist * 4 + 3,
        [gen, n = std::move(n)] {
            auto d = std::make_shared<DecorationResult>();
            d->source = n.center;
            gen->decorate(n, *d);
            return std::shared_ptr<const DecorationResult>(std::move(d));
        },
        [](Entry& en, std::shared_ptr<const DecorationResult> d) {
            en.decoration = std::move(d);
            en.stage = ChunkStage::Decorated;
        });
}

void ChunkManager::submitMerge(Entry& e) {
    std::array<std::shared_ptr<const DecorationResult>, 9> decos;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx)
            decos[size_t((dz + 1) * 3 + dx + 1)] = find({e.pos.x + dx, e.pos.z + dz})->decoration;
    const BlockRegistry* reg = &registry_;
    auto gen = generator_;
    struct Merged {
        std::unique_ptr<Chunk> chunk;
        Json extra;
        bool fromDisk = false;
    };
    submit(
        e, e.dist * 4 + 2,
        [reg, gen, terrain = e.terrain, decos, pos = e.pos, storage = storage_]() -> Merged {
            // A saved chunk replaces the generated one (its neighbors still got this
            // chunk's DECORATE writes, so trees crossing the border stay whole).
            if (storage && storage->exists(pos)) {
                Merged m;
                std::string err;
                m.chunk = storage->load(pos, &m.extra, &err);
                if (m.chunk) {
                    m.fromDisk = true;
                    return m;
                }
                log::warn("chunk {} {}: {}", pos.x, pos.z, err.empty() ? "unreadable save" : err);
            }
            auto c = std::make_unique<Chunk>(*terrain); // shares section storage until written
            struct Pending {
                uint8_t step;
                uint16_t feature;
                uint8_t source;
                uint32_t seq;
                const BlockWrite* write;
            };
            std::vector<Pending> pending;
            for (size_t i = 0; i < decos.size(); ++i) {
                if (!decos[i]) continue;
                const auto& writes = decos[i]->writes;
                for (size_t k = 0; k < writes.size(); ++k)
                    if (ChunkPos::of(writes[k].pos) == pos)
                        pending.push_back({writes[k].step, writes[k].feature, uint8_t(i), uint32_t(k), &writes[k]});
            }
            // Fixed order regardless of which job finished first: deterministic worlds.
            // Feature-major within a step, so (say) every source's trees land before
            // any source's flowers, as vanilla's per-step feature order intends.
            std::sort(pending.begin(), pending.end(), [](const Pending& a, const Pending& b) {
                if (a.step != b.step) return a.step < b.step;
                if (a.feature != b.feature) return a.feature < b.feature;
                if (a.source != b.source) return a.source < b.source;
                return a.seq < b.seq;
            });
            const auto leaves = reg->tag("leaves"), byTrees = reg->tag("replaceable_by_trees");
            std::vector<BlockPos> placed;
            for (const Pending& p : pending) {
                const BlockWrite& w = *p.write;
                const int lx = w.pos.x & 15, lz = w.pos.z & 15;
                const StateId cur = c->get(lx, w.pos.y, lz);
                bool apply = w.policy == kWriteForce || cur == w.expect;
                if (!apply && w.policy == kWriteTree)
                    apply = (reg->info(cur).flags & kAir) || (leaves && reg->hasTag(cur, *leaves)) ||
                            (byTrees && reg->hasTag(cur, *byTrees));
                if (apply) {
                    c->set(lx, w.pos.y, lz, w.state);
                    placed.push_back(w.pos);
                }
            }
            // Post-processing (vanilla marks feature blocks for it): a plant or dripstone
            // whose support in its own column was replaced by another source's feature
            // (moss under an azalea turned into a clay pool) is removed, bottom up so
            // stacked blocks follow their base.
            struct ChunkView final : BlockGetter {
                const BlockRegistry& r;
                const Chunk& chunk;
                ChunkView(const BlockRegistry& reg, const Chunk& ch) : r(reg), chunk(ch) {}
                const BlockRegistry& registry() const override { return r; }
                StateId getBlock(const BlockPos& p) const override {
                    return ChunkPos::of(p) == chunk.pos ? chunk.get(p.x & 15, p.y, p.z & 15) : StateId(0);
                }
            };
            const ChunkView view(*reg, *c);
            std::sort(placed.begin(), placed.end(), [](const BlockPos& a, const BlockPos& b) { return a.y < b.y; });
            const StateId water = reg->defaultState("water");
            for (const BlockPos& p : placed) {
                const StateId s = c->get(p.x & 15, p.y, p.z & 15);
                if ((reg->info(s).flags & kAir) || !reg->rules().supportedFromColumn(s)) continue;
                if (reg->rules().canSurvive(view, s, p)) continue;
                const bool wet = reg->boolValue(s, "waterlogged") || reg->info(s).flags & kWaterlogged;
                c->set(p.x & 15, p.y, p.z & 15, wet ? water : BlockRegistry::kAirState);
            }
            gen->finalizeChunk(*c);
            return Merged{std::move(c), Json(), false};
        },
        [this](Entry& en, Merged m) {
            en.chunk = std::move(m.chunk);
            en.savedVersion = en.chunk->blockVersion;
            en.stage = ChunkStage::Final;
            readyDirty_ = true;
            if (m.fromDisk && loadedExtra_ && !m.extra.is_null()) loadedExtra_(en.pos, m.extra);
        });
}

void ChunkManager::submitLight(Entry& e) {
    if (!lightFn_) {
        e.stage = ChunkStage::Lit; // no light engine: uniform full skylight
        becameLit(e);
        return;
    }
    Neighborhood n = neighborhood(e.pos);
    auto fn = lightFn_;
    auto copy = std::make_unique<Chunk>(*e.chunk); // light is computed into a copy, then swapped in
    submit(
        e, e.dist * 4 + 1,
        [fn, n = std::move(n), copy = std::move(copy)]() mutable {
            fn(n, *copy);
            return std::move(copy);
        },
        [this](Entry& en, std::unique_ptr<Chunk> lit) {
            for (int i = 0; i < kSectionCount; ++i) {
                en.chunk->sections[size_t(i)].light = lit->sections[size_t(i)].light;
                en.chunk->sections[size_t(i)].uniformLight = lit->sections[size_t(i)].uniformLight;
            }
            ++en.chunk->lightVersion;
            en.stage = ChunkStage::Lit;
            becameLit(en);
        });
}

void ChunkManager::becameLit(Entry& e) {
    (void)e;
    readyDirty_ = true;
}

void ChunkManager::update() {
    if (viewChanged_) {
        viewChanged_ = false;
        const int load = renderDistance_ + 4;
        const int keep = renderDistance_ + 5;
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (it->first.chebyshev(center_) > keep) {
                if (it->second->meshReady) dropped_.push_back(it->first);
                if (storage_ && it->second->dirty()) submitSave(*it->second, true);
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
        for (int dz = -load; dz <= load; ++dz)
            for (int dx = -load; dx <= load; ++dx) {
                const ChunkPos p{center_.x + dx, center_.z + dz};
                if (find(p)) continue;
                auto e = std::make_unique<Entry>();
                e->pos = p;
                e->id = nextId_++;
                entries_.emplace(p, std::move(e));
            }
        ordered_.clear();
        for (auto& [p, e] : entries_) {
            e->dist = p.chebyshev(center_);
            ordered_.push_back(e.get());
        }
        std::sort(ordered_.begin(), ordered_.end(), [this](const Entry* a, const Entry* b) {
            if (a->dist != b->dist) return a->dist < b->dist;
            const int da = (a->pos.x - center_.x) * (a->pos.x - center_.x) + (a->pos.z - center_.z) * (a->pos.z - center_.z);
            const int db = (b->pos.x - center_.x) * (b->pos.x - center_.x) + (b->pos.z - center_.z) * (b->pos.z - center_.z);
            if (da != db) return da < db;
            return a->pos.key() < b->pos.key();
        });
        readyDirty_ = true;
    }

    const int maxInFlight = int(std::max(16u, jobs_.workerCount() * 3));
    const int r = renderDistance_;
    for (Entry* e : ordered_) {
        if (inFlight_ >= maxInFlight) break;
        if (e->busy) continue;
        switch (e->stage) {
        case ChunkStage::Empty: submitTerrain(*e); break;
        case ChunkStage::Terrain:
            if (e->dist <= r + 3 && neighborsAtLeast(*e, ChunkStage::Terrain)) submitDecorate(*e);
            break;
        case ChunkStage::Decorated:
            if (e->dist <= r + 2 && neighborsAtLeast(*e, ChunkStage::Decorated) && !savesInFlight_.contains(e->pos))
                submitMerge(*e);
            break;
        case ChunkStage::Final:
            if (e->dist <= r + 1 && neighborsAtLeast(*e, ChunkStage::Final)) submitLight(*e);
            break;
        case ChunkStage::Lit: break;
        }
    }
    if (readyDirty_) refreshMeshReady();
}

void ChunkManager::refreshMeshReady() {
    readyDirty_ = false;
    for (Entry* e : ordered_) {
        const bool ready = e->dist <= renderDistance_ && e->stage == ChunkStage::Lit &&
                           neighborsAtLeast(*e, ChunkStage::Lit);
        if (ready && !e->meshReady) {
            e->meshReady = true;
            e->meshDirty = 0;
            for (int i = 0; i < kSectionCount; ++i)
                if (e->chunk->sections[size_t(i)].blocks) e->meshDirty |= 1u << i;
        } else if (!ready && e->meshReady) {
            e->meshReady = false;
            e->meshDirty = 0;
            dropped_.push_back(e->pos);
        }
    }
}

Chunk* ChunkManager::chunk(ChunkPos p) {
    Entry* e = find(p);
    return e ? e->chunk.get() : nullptr;
}

const Chunk* ChunkManager::chunk(ChunkPos p) const {
    const Entry* e = find(p);
    return e ? e->chunk.get() : nullptr;
}

ChunkStage ChunkManager::stage(ChunkPos p) const {
    const Entry* e = find(p);
    return e ? e->stage : ChunkStage::Empty;
}

bool ChunkManager::isReady(ChunkPos p) const {
    const Entry* e = find(p);
    return e && e->stage == ChunkStage::Lit && neighborsAtLeast(*e, ChunkStage::Lit);
}

StateId ChunkManager::getBlock(const BlockPos& p) const {
    const Chunk* c = chunk(ChunkPos::of(p));
    return c ? c->get(p.x & 15, p.y, p.z & 15) : StateId(0);
}

bool ChunkManager::setBlock(const BlockPos& p, StateId state) {
    const ChunkPos cp = ChunkPos::of(p);
    Entry* e = find(cp);
    if (!e || !e->chunk || !isReady(cp) || p.y < kWorldMinY || p.y > kWorldMaxY) return false;
    if (e->chunk->set(p.x & 15, p.y, p.z & 15, state) == state) return true;
    ++e->chunk->blockVersion;
    markMeshDirty(p);
    if (lightEngine_) {
        lightChanged_.clear();
        lightEngine_->update(*this, p, lightChanged_);
        // Smooth lighting reads up to two cells away from a face: remesh every section
        // overlapping the 5x5x5 box around each changed cell (deduplicated).
        std::unordered_set<int64_t> sections;
        std::unordered_set<int64_t> chunksTouched;
        for (const BlockPos& c : lightChanged_) {
            chunksTouched.insert(ChunkPos::of(c).key());
            const int x0 = floorDiv(c.x - 2, 16), x1 = floorDiv(c.x + 2, 16);
            const int z0 = floorDiv(c.z - 2, 16), z1 = floorDiv(c.z + 2, 16);
            const int s0 = sectionIndexOfY(std::max(c.y - 2, kWorldMinY));
            const int s1 = sectionIndexOfY(std::min(c.y + 2, kWorldMaxY));
            for (int cz = z0; cz <= z1; ++cz)
                for (int cx = x0; cx <= x1; ++cx)
                    for (int si = s0; si <= s1; ++si)
                        if (sections.insert((ChunkPos(cx, cz).key() << 5) ^ si).second) markSectionDirty({cx, cz}, si);
        }
        for (int64_t key : chunksTouched) {
            const ChunkPos cp(int(key >> 32), int(int32_t(uint32_t(key))));
            if (Chunk* c = chunk(cp)) ++c->lightVersion;
        }
    }
    return true;
}

void ChunkManager::markSectionDirty(ChunkPos c, int si) {
    if (si < 0 || si >= kSectionCount) return;
    Entry* e = find(c);
    if (e && e->meshReady) e->meshDirty |= 1u << si;
}

void ChunkManager::markMeshDirty(const BlockPos& p) {
    // Every section overlapping the 3x3x3 block box around p (AO and culling read one block over).
    const int x0 = floorDiv(p.x - 1, 16), x1 = floorDiv(p.x + 1, 16);
    const int z0 = floorDiv(p.z - 1, 16), z1 = floorDiv(p.z + 1, 16);
    const int s0 = sectionIndexOfY(std::max(p.y - 1, kWorldMinY)), s1 = sectionIndexOfY(std::min(p.y + 1, kWorldMaxY));
    for (int cz = z0; cz <= z1; ++cz)
        for (int cx = x0; cx <= x1; ++cx)
            for (int si = s0; si <= s1; ++si) markSectionDirty({cx, cz}, si);
}

void ChunkManager::takeMeshWork(std::vector<MeshWork>& out, size_t maxChunks) {
    for (Entry* e : ordered_) {
        if (out.size() >= maxChunks) break;
        if (!e->meshReady || e->meshDirty == 0) continue;
        out.push_back({e->pos, e->meshDirty, neighborhood(e->pos)});
        e->meshDirty = 0;
    }
}

std::vector<ChunkPos> ChunkManager::takeDropped() {
    std::vector<ChunkPos> out;
    out.swap(dropped_);
    return out;
}

bool ChunkManager::viewReady() const {
    if (viewChanged_ || readyDirty_ || ordered_.empty()) return false;
    for (const Entry* e : ordered_) {
        if (e->dist > renderDistance_) break;
        if (!e->meshReady) return false;
    }
    return true;
}

bool ChunkManager::viewMeshed() const {
    if (!viewReady()) return false;
    for (const Entry* e : ordered_) {
        if (e->dist > renderDistance_) break;
        if (e->meshDirty) return false;
    }
    return true;
}

Neighborhood ChunkManager::neighborhood(ChunkPos center) const {
    Neighborhood n;
    n.center = center;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) {
            const Entry* e = find({center.x + dx, center.z + dz});
            if (e && e->chunk) n.cols[size_t((dz + 1) * 3 + dx + 1)] = ColumnSnapshot::of(*e->chunk);
        }
    return n;
}

ChunkManager::Stats ChunkManager::stats() const {
    Stats s;
    s.entries = int(entries_.size());
    s.inFlight = inFlight_;
    for (const auto& [p, e] : entries_) {
        switch (e->stage) {
        case ChunkStage::Empty: break;
        case ChunkStage::Terrain: ++s.terrain; break;
        case ChunkStage::Decorated: ++s.decorated; break;
        case ChunkStage::Final: ++s.final; break;
        case ChunkStage::Lit: ++s.lit; break;
        }
        if (e->meshReady) ++s.meshReady;
    }
    return s;
}

void ChunkManager::submitSave(Entry& e, bool unloading) {
    // Snapshot: sections are shared copy-on-write, so later edits never reach the job.
    auto snapshot = std::make_shared<const Chunk>(*e.chunk);
    Json extra = saveExtra_ ? saveExtra_(e.pos, unloading) : Json();
    e.savedVersion = e.chunk->blockVersion;
    ++savesInFlight_[e.pos];
    std::weak_ptr<int> token = alive_;
    JobSystem* jobs = &jobs_;
    jobs_.submit(-1, [this, token, jobs, snapshot, extra = std::move(extra), storage = storage_]() {
        std::string err;
        if (!storage->save(*snapshot, extra, &err)) log::error("saving chunk {} {}: {}", snapshot->pos.x, snapshot->pos.z, err);
        jobs->postMain([this, token, pos = snapshot->pos] {
            if (token.expired()) return;
            if (--savesInFlight_[pos] <= 0) savesInFlight_.erase(pos);
        });
    });
}

int ChunkManager::saveAll() {
    if (!storage_) return 0;
    int n = 0;
    for (auto& [p, e] : entries_)
        if (e->dirty()) {
            submitSave(*e, false);
            ++n;
        }
    return n;
}

} // namespace mc
