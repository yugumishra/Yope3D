#pragma once
#include "../ecs/Entity.h"
#include <cstdint>
#include <vector>
#include <utility>

namespace ecs { class Registry; }

namespace physics {

// Dual-method broadphase: entities.size() below PhysicsConstants::SAP_METHOD_SWITCH_N uses
// an incrementally-maintained 1D sweep-and-prune (cheap constant factor, the right choice for
// most scenes); at or above it uses a uniform grid (better N-scaling, more per-frame fixed
// overhead — see tools/CLAUDE.md Phase E findings for the measured crossover). collectPairs()
// is a one-line dispatcher; collectPairsSweep()/collectPairsGrid() are public so tests and
// benchmarking can force a specific method regardless of N.
//
// A third tier (plain rebuild-every-frame sweep below a lower N threshold) was tried and
// reverted: measured same-session against the incremental sweep, its lack of upsert/prune
// bookkeeping saved only a few µs (computeBounds dominates the build-phase cost either way),
// while its full std::sort every call cost more than the incremental resort saved — net a wash
// at N=500 and worse at N=1000. Not worth the extra method/state.
class BroadphaseSAP {
public:
    void collectPairs(const std::vector<ecs::Entity>& entities,
                      ecs::Registry& reg,
                      std::vector<std::pair<ecs::Entity, ecs::Entity>>& out);

    // Incrementally-maintained sweep-and-prune. Sorts sweepEntries_ by minX (entity.id
    // tie-break) — persists frame-to-frame; positions are refreshed in place and the array is
    // kept close-to-sorted via a capped adjacent-swap pass instead of a full re-sort each call
    // (falls back to a full sort if the data has drifted too far, e.g. right after a long
    // stretch spent in grid mode — see BroadphaseSAP.cpp for why that needs no special-casing).
    void collectPairsSweep(const std::vector<ecs::Entity>& entities,
                           ecs::Registry& reg,
                           std::vector<std::pair<ecs::Entity, ecs::Entity>>& out);

    // Uniform-grid broadphase. Computes a world-space AABB per tangible entity, then inserts it
    // into every grid cell its AABB spans (entries too large for the grid go to a separate
    // giants_ list instead, tested against everything directly). Candidate pairs come from
    // within-bucket scans only — no neighbor-cell scan is needed, since inserting into the full
    // span guarantees any two truly-overlapping entries share at least one cell (see
    // overlaps3D call site in the .cpp for the argument). Multi-cell entries can still produce
    // the same candidate more than once (once per shared cell); the output is canonicalized
    // (min-id, max-id) and sorted+uniqued, which both deduplicates and makes the result
    // deterministic regardless of cell-processing order or entity iteration order. Rebuilds
    // fresh every call — no incremental maintenance (see class-level rationale in CLAUDE.md).
    void collectPairsGrid(const std::vector<ecs::Entity>& entities,
                          ecs::Registry& reg,
                          std::vector<std::pair<ecs::Entity, ecs::Entity>>& out);

private:
    struct Entry {
        float minX, maxX;
        float minY, maxY;
        float minZ, maxZ;
        ecs::Entity entity = ecs::NullEntity;
    };

    // Shared per-entity bounds computation (Hull/tangible/Transform read, compound-collider
    // 8-corner sweep, OBB rotation-fattening, sphere/AABB extents) — used by both methods so
    // this geometry logic (with real bug history, see the OBB comment in the .cpp) lives once.
    // Returns false (skip) if the entity lacks Hull/tangible/Transform/a known shape form.
    static bool computeBounds(ecs::Entity e, ecs::Registry& reg, Entry& out);

    struct CellKey {
        int32_t x, y, z;
        bool operator<(const CellKey& o) const {
            if (x != o.x) return x < o.x;
            if (y != o.y) return y < o.y;
            return z < o.z;
        }
        bool operator==(const CellKey& o) const { return x == o.x && y == o.y && z == o.z; }
    };

    struct CellSlot {
        CellKey key;
        uint32_t entryIndex; // index into entries_
        // Copy of entries_[entryIndex].entity.id, not just a lookup key: dense
        // grid scenarios put many bodies in one cell (key ties are the common
        // case, not the exception), so the sort's tie-break must never indirect
        // into entries_ — that turns a contiguous-array sort into a
        // cache-missing one on exactly the comparisons that matter most.
        uint32_t entityId;
    };

    static bool overlaps3D(const Entry& a, const Entry& b);
    static bool sweepLess(const Entry& a, const Entry& b);

    // --- grid state (collectPairsGrid) — rebuilt fresh every call ---
    std::vector<Entry>    entries_;
    std::vector<uint32_t> giants_;       // indices into entries_ whose cell span exceeds the cap
    std::vector<CellSlot> cellEntries_;  // (cell, entry index) pairs, sorted by key after build
    std::vector<uint8_t>  isGiant_;      // scratch: entries_ index -> is-a-giant, sized on demand

    // --- sweep state (collectPairsSweep) — persists frame-to-frame ---
    std::vector<Entry>    sweepEntries_;  // compacted, no holes, kept close-to-sorted by minX
    std::vector<int32_t>  entityToSlot_;  // entity.id -> slot in sweepEntries_, -1 = absent
    std::vector<uint8_t>  touchedSlot_;   // scratch: slot -> touched-this-call, sized per call
};

} // namespace physics
