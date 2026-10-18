// ChunkManager.h -- the staged, deterministic chunk pipeline (docs/DESIGN.md).
//
//   TERRAIN(c)   pure function of (seed, c)
//   DECORATE(c)  needs TERRAIN of the 3x3; reads only those immutable snapshots
//   MERGE(c)     needs DECORATE of the 3x3; applies their writes -> block-final
//   LIGHT(c)     needs block-final 3x3 -> lit
//   (meshing)    a lit chunk whose 8 neighbors are lit is "mesh-ready"
//
// A chunk visible at render distance R needs: lit R+1, final R+2, decorated R+3,
// terrain R+4. The main thread owns all entries; jobs get snapshots and post
// results back through the JobSystem, so no locks guard world data.
#pragma once

#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/JobSystem.h"
#include "core/Json.h"
#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkSnapshot.h"
#include "world/gen/ChunkGenerator.h"

namespace mc {

enum class ChunkStage : uint8_t { Empty, Terrain, Decorated, Final, Lit };

class LightEngine;
class ChunkStorage;

class ChunkManager {
public:
    // Computes the light of the center chunk from the final 3x3 neighborhood,
    // writing light sections into `out` (a copy of the center chunk's sections).
    using LightFn = std::function<void(const Neighborhood& blocks, Chunk& out)>;

    struct Stats {
        int entries = 0, terrain = 0, decorated = 0, final = 0, lit = 0, meshReady = 0, inFlight = 0;
    };

    ChunkManager(const BlockRegistry& registry, std::shared_ptr<const ChunkGenerator> generator, JobSystem& jobs);
    ~ChunkManager();
    ChunkManager(const ChunkManager&) = delete;
    ChunkManager& operator=(const ChunkManager&) = delete;

    void setLightFunction(LightFn fn) { lightFn_ = std::move(fn); }

    // Persistence: edited chunks are saved on unload and by saveAll(); chunks with a
    // save file load it in place of their MERGE result. The hooks carry per-chunk
    // extras (scheduled ticks): `saveExtra(pos, unloading)` collects them, `loadedExtra`
    // hands them back on load.
    void setStorage(std::shared_ptr<const ChunkStorage> storage) { storage_ = std::move(storage); }
    // `extraDirty(pos)` says a chunk must be saved for its extras although no block changed.
    void setPersistenceHooks(std::function<Json(ChunkPos, bool unloading)> saveExtra,
                             std::function<void(ChunkPos, const Json&)> loadedExtra,
                             std::function<bool(ChunkPos)> extraDirty = {}) {
        saveExtra_ = std::move(saveExtra);
        loadedExtra_ = std::move(loadedExtra);
        extraDirty_ = std::move(extraDirty);
    }
    // Saves every edited chunk (in jobs); returns how many were queued.
    int saveAll();
    bool savesPending() const { return !savesInFlight_.empty(); }
    // Uses `engine` for the LIGHT stage and for incremental updates after edits.
    void setLightEngine(std::shared_ptr<const LightEngine> engine);
    const LightEngine* lightEngine() const { return lightEngine_.get(); }

    // Chunks within `renderDistance` of `center` become mesh-ready; the pyramid
    // around them is generated as well. Entries beyond renderDistance + 5 unload.
    void setView(ChunkPos center, int renderDistance);
    // Main thread, once per frame: schedules jobs (results arrive via JobSystem::drainMain).
    void update();

    // Block-final chunk or nullptr.
    Chunk* chunk(ChunkPos p);
    const Chunk* chunk(ChunkPos p) const;
    ChunkStage stage(ChunkPos p) const;
    // Lit and all 8 neighbors lit: edits, ticking and meshing are allowed.
    bool isReady(ChunkPos p) const;

    StateId getBlock(const BlockPos& p) const;
    // Edits a ready chunk; marks the affected sections (and border neighbors) for remeshing.
    // Returns false if the chunk is not ready.
    bool setBlock(const BlockPos& p, StateId state);
    // Marks sections for remeshing (used by light updates).
    void markMeshDirty(const BlockPos& p);
    void markSectionDirty(ChunkPos c, int sectionIndex);
    // Every meshed section, e.g. after a change to how meshes are built (smooth lighting).
    void markAllMeshesDirty();

    // Mesh scheduling for the renderer: ready chunks with dirty sections, nearest first.
    struct MeshWork {
        ChunkPos pos;
        uint32_t sections; // bit per section index
        Neighborhood neighborhood;
    };
    void takeMeshWork(std::vector<MeshWork>& out, size_t maxChunks);
    // Chunks that stopped being mesh-ready since the last call (unloaded).
    std::vector<ChunkPos> takeDropped();

    // Chunks that tick (random ticks, weather): the mesh-ready ones, within the render
    // distance with every neighbor lit.
    std::vector<ChunkPos> tickingChunks() const;
    // Chunks that started ticking since the last call, the first time only; `generated`
    // when they came from the generator rather than a save (vanilla post-processing).
    struct NewlyTicking {
        ChunkPos pos;
        bool generated = false;
    };
    std::vector<NewlyTicking> takeNewlyTicking() { return std::exchange(newlyTicking_, {}); }

    // Every chunk within the render distance is mesh-ready (meshes may still be dirty).
    bool viewReady() const;
    // viewReady() and no section waits for (re)meshing.
    bool viewMeshed() const;

    Neighborhood neighborhood(ChunkPos center) const;
    Stats stats() const;
    ChunkPos center() const { return center_; }
    int renderDistance() const { return renderDistance_; }
    const BlockRegistry& registry() const { return registry_; }
    const ChunkGenerator& generator() const { return *generator_; }

private:
    struct Entry {
        ChunkPos pos;
        uint64_t id = 0;
        ChunkStage stage = ChunkStage::Empty;
        bool busy = false;
        bool meshReady = false;
        uint32_t meshDirty = 0;
        int dist = 0; // Chebyshev distance from the view center
        std::shared_ptr<const Chunk> terrain;
        std::shared_ptr<const DecorationResult> decoration;
        std::unique_ptr<Chunk> chunk;
        uint32_t savedVersion = 0; // chunk->blockVersion when last loaded or saved
        bool generated = false;    // from the generator (not a save)
        bool startedTicking = false;
        bool dirty() const { return chunk && chunk->blockVersion != savedVersion; }
    };
    void submitSave(Entry& e, bool unloading);

    Entry* find(ChunkPos p);
    const Entry* find(ChunkPos p) const;
    bool neighborsAtLeast(const Entry& e, ChunkStage s) const;
    void submitTerrain(Entry& e);
    void submitDecorate(Entry& e);
    void submitMerge(Entry& e);
    void submitLight(Entry& e);
    void becameLit(Entry& e);
    void refreshMeshReady();
    template <class Work, class Done>
    void submit(Entry& e, int priority, Work work, Done done);

    const BlockRegistry& registry_;
    std::shared_ptr<const ChunkGenerator> generator_;
    JobSystem& jobs_;
    LightFn lightFn_;
    std::shared_ptr<const LightEngine> lightEngine_;
    std::vector<BlockPos> lightChanged_;
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);

    std::unordered_map<ChunkPos, std::unique_ptr<Entry>> entries_;
    std::vector<Entry*> ordered_; // by distance from the center
    ChunkPos center_{0, 0};
    int renderDistance_ = 8;
    bool viewChanged_ = true;
    bool readyDirty_ = true;
    uint64_t nextId_ = 1;
    int inFlight_ = 0;
    std::vector<ChunkPos> dropped_;
    std::shared_ptr<const ChunkStorage> storage_;
    std::vector<NewlyTicking> newlyTicking_;
    std::function<Json(ChunkPos, bool)> saveExtra_;
    std::function<void(ChunkPos, const Json&)> loadedExtra_;
    std::function<bool(ChunkPos)> extraDirty_;
    bool needsSave(const Entry& e) const { return e.chunk && (e.dirty() || (extraDirty_ && extraDirty_(e.pos))); }
    std::unordered_map<ChunkPos, int> savesInFlight_; // count per chunk
};

} // namespace mc
