#include "BroadphaseSAP.h"
#include "CompoundShape.h"
#include "PhysicsConstants.h"
#include "../ecs/Registry.h"
#include "../ecs/Components.h"
#include "../world/Transform.h"
#include "../debug/Profiler.h"
#include <algorithm>
#include <cassert>
#include <cmath>

namespace physics {

bool BroadphaseSAP::overlaps3D(const Entry& a, const Entry& b) {
    if (a.maxX < b.minX || b.maxX < a.minX) return false;
    if (a.maxY < b.minY || b.maxY < a.minY) return false;
    if (a.maxZ < b.minZ || b.maxZ < a.minZ) return false;
    return true;
}

bool BroadphaseSAP::sweepLess(const Entry& a, const Entry& b) {
    if (a.minX != b.minX) return a.minX < b.minX;
    return a.entity.id < b.entity.id;
}

bool BroadphaseSAP::computeBounds(ecs::Entity e, ecs::Registry& reg, Entry& out) {
    auto* hc = reg.get<ecs::Hull>(e);
    if (!hc || !hc->tangible) return false;
    // Note: fixed entities are included so dynamic-vs-fixed pairs are generated.
    // Fixed-fixed pairs are filtered in advance() after broadphase.
    auto* tf = reg.get<Transform>(e);
    if (!tf) return false;
    math::Vec3 pos = tf->position;
    math::Vec3 ext{};
    // Compound collider: world AABB of the baked local bounds. Center may
    // be offset from the body origin, so emit an explicit min/max entry
    // rather than the symmetric pos±ext used by the primitive forms.
    if (auto* cc = reg.get<ecs::CompoundCollider>(e); cc && cc->compiled && !cc->compiled->nodes.empty()) {
        const math::Mat3 R = math::Mat3::rotation(tf->rotation);
        const math::Vec3& lmn = cc->compiled->localMin;
        const math::Vec3& lmx = cc->compiled->localMax;
        math::Vec3 wmn{ 1e30f,  1e30f,  1e30f};
        math::Vec3 wmx{-1e30f, -1e30f, -1e30f};
        for (int c = 0; c < 8; ++c) {
            math::Vec3 corner{ (c & 1) ? lmx.x : lmn.x, (c & 2) ? lmx.y : lmn.y, (c & 4) ? lmx.z : lmn.z };
            math::Vec3 w = pos + R * corner;
            wmn.x = std::min(wmn.x, w.x); wmn.y = std::min(wmn.y, w.y); wmn.z = std::min(wmn.z, w.z);
            wmx.x = std::max(wmx.x, w.x); wmx.y = std::max(wmx.y, w.y); wmx.z = std::max(wmx.z, w.z);
        }
        out = { wmn.x, wmx.x, wmn.y, wmx.y, wmn.z, wmx.z, e };
        return true;
    }
    if (auto* sf = reg.get<ecs::SphereForm>(e))
        ext = {sf->radius, sf->radius, sf->radius};
    else if (auto* af = reg.get<ecs::AABBForm>(e))
        ext = af->extent;
    else if (auto* of = reg.get<ecs::OBBForm>(e)) {
        // World AABB of a rotated box: half-extent along world axis i
        // is sum_j |R[i][j]| * ext[j]. Using the unrotated extents
        // under-covers (a rotated cube's world AABB is up to sqrt(3)x
        // larger), silently dropping pairs for corner contacts.
        // Column-major: R[i][j] = m[3j + i].
        const math::Mat3 R = math::Mat3::rotation(tf->rotation);
        const math::Vec3& x = of->extent;
        ext = {
            std::fabs(R.m[0]) * x.x + std::fabs(R.m[3]) * x.y + std::fabs(R.m[6]) * x.z,
            std::fabs(R.m[1]) * x.x + std::fabs(R.m[4]) * x.y + std::fabs(R.m[7]) * x.z,
            std::fabs(R.m[2]) * x.x + std::fabs(R.m[5]) * x.y + std::fabs(R.m[8]) * x.z,
        };
    }
    else
        return false; // no known shape
    out = {
        pos.x - ext.x, pos.x + ext.x,
        pos.y - ext.y, pos.y + ext.y,
        pos.z - ext.z, pos.z + ext.z,
        e
    };
    return true;
}

void BroadphaseSAP::collectPairs(const std::vector<ecs::Entity>& entities,
                                  ecs::Registry& reg,
                                  std::vector<std::pair<ecs::Entity, ecs::Entity>>& out)
{
    if (entities.size() >= SAP_METHOD_SWITCH_N) collectPairsGrid(entities, reg, out);
    else                                        collectPairsSweep(entities, reg, out);
}

void BroadphaseSAP::collectPairsSweep(const std::vector<ecs::Entity>& entities,
                                       ecs::Registry& reg,
                                       std::vector<std::pair<ecs::Entity, ecs::Entity>>& out)
{
    out.clear();

    // Size entityToSlot_ once via a max-id pre-scan (amortized growth, avoids a
    // reallocation per new id inside the main loop — same reasoning IslandDetector's
    // own max-id pre-pass uses).
    uint32_t maxId = 0;
    for (ecs::Entity e : entities) maxId = std::max(maxId, e.id);
    if (entityToSlot_.size() <= maxId)
        entityToSlot_.resize(maxId + 1, -1);

    const size_t prevCount = sweepEntries_.size();
    touchedSlot_.assign(prevCount, 0);

    // Upsert: update existing slots in place, append new ones. A touch always
    // overwrites the WHOLE Entry (bounds and .entity), never bounds-only — this is
    // what makes id-recycling (destroy+recreate reusing an id) and cross-registry
    // slot aliasing self-heal instead of leaking a stale entity.
    {
        YOPE_PROF_SCOPE("sap_sweep_build", "physics");
        for (ecs::Entity e : entities) {
            Entry en;
            if (!computeBounds(e, reg, en)) continue;
            int32_t slot = entityToSlot_[e.id];
            if (slot != -1 && static_cast<size_t>(slot) < prevCount) {
                sweepEntries_[slot] = en;
                touchedSlot_[slot] = 1;
            } else {
                int32_t newSlot = static_cast<int32_t>(sweepEntries_.size());
                sweepEntries_.push_back(en);
                entityToSlot_[e.id] = newSlot;
            }
        }

        // Prune: untouched slots in [0, prevCount) get swap-removed with the tail.
        // Touched-flag is carried forward with the relocated element so it's
        // correctly re-examined at its new position — handles "remove the last
        // element" and "swap-source was itself touched" with no special-casing.
        size_t i = 0;
        while (i < prevCount && i < sweepEntries_.size()) {
            if (touchedSlot_[i]) { ++i; continue; }
            size_t last = sweepEntries_.size() - 1;
            ecs::Entity removedId = sweepEntries_[i].entity;
            if (i != last) {
                sweepEntries_[i] = sweepEntries_[last];
                entityToSlot_[sweepEntries_[i].entity.id] = static_cast<int32_t>(i);
                touchedSlot_[i] = (last < prevCount) ? touchedSlot_[last] : 1;
            }
            entityToSlot_[removedId.id] = -1;
            sweepEntries_.pop_back();
            // Do not advance i — sweepEntries_[i] now holds the relocated element
            // (or, if i == last, the array simply shrank past i and the loop
            // condition ends it next check).
        }
    }

    // Incremental resort: sweepEntries_ was fully sorted at the end of the last
    // call, and touched entries only shift by one substep's motion, so a capped
    // adjacent-swap pass keeps it sorted in amortized O(N) instead of O(N log N)
    // with a real comparator. Swap budget caps total work at O(N), so "attempt
    // incremental resort, detect overflow, fall back to a full sort" is never
    // worse than an unconditional std::sort, and cheaper whenever the data is
    // close to sorted (the common case). This also self-corrects for free on the
    // first call back in sweep mode after a long stretch in grid mode — a stale
    // array is far from sorted relative to current positions, so it blows the
    // budget immediately and falls back to a full sort, with no "did we just
    // switch methods" logic needed anywhere.
    {
        YOPE_PROF_SCOPE_N("sap_sweep_sort", "physics", sweepEntries_.size());
        size_t swaps = 0;
        const size_t swapBudget = sweepEntries_.size();
        bool overflowed = false;
        for (size_t i = 1; i < sweepEntries_.size() && !overflowed; ++i) {
            size_t j = i;
            while (j > 0 && sweepLess(sweepEntries_[j], sweepEntries_[j - 1])) {
                std::swap(sweepEntries_[j], sweepEntries_[j - 1]);
                entityToSlot_[sweepEntries_[j].entity.id]     = static_cast<int32_t>(j);
                entityToSlot_[sweepEntries_[j - 1].entity.id] = static_cast<int32_t>(j - 1);
                --j;
                if (++swaps > swapBudget) { overflowed = true; break; }
            }
        }
        if (overflowed) {
            std::sort(sweepEntries_.begin(), sweepEntries_.end(), sweepLess);
            for (size_t k = 0; k < sweepEntries_.size(); ++k)
                entityToSlot_[sweepEntries_[k].entity.id] = static_cast<int32_t>(k);
        }
    }

#ifndef NDEBUG
    assert(std::is_sorted(sweepEntries_.begin(), sweepEntries_.end(), sweepLess));
#endif

    // Sweep with X-axis pruning, Y/Z overlap test. i<j over one sorted array
    // structurally emits each pair exactly once — unlike grid mode, no
    // dedup/final-sort is needed here.
    {
        YOPE_PROF_SCOPE("sap_sweep_query", "physics");
        for (size_t i = 0; i < sweepEntries_.size(); ++i) {
            const Entry& a = sweepEntries_[i];
            for (size_t j = i + 1; j < sweepEntries_.size(); ++j) {
                const Entry& b = sweepEntries_[j];
                if (b.minX > a.maxX) break;
                if (b.minY > a.maxY || b.maxY < a.minY) continue;
                if (b.minZ > a.maxZ || b.maxZ < a.minZ) continue;
                out.emplace_back(a.entity, b.entity);
            }
        }
    }
}

void BroadphaseSAP::collectPairsGrid(const std::vector<ecs::Entity>& entities,
                                      ecs::Registry& reg,
                                      std::vector<std::pair<ecs::Entity, ecs::Entity>>& out)
{
    out.clear();
    entries_.clear();
    giants_.clear();
    cellEntries_.clear();

    // Phase 1 — build entries from registry. Linear in N but each entity pays
    // 3-5 archetype lookups (Hull, Transform, plus one shape form).
    {
        YOPE_PROF_SCOPE("sap_build", "physics");
        for (ecs::Entity e : entities) {
            Entry en;
            if (computeBounds(e, reg, en)) entries_.push_back(en);
        }
    }

    // Phase 2 — bucket entries into a fixed-size uniform grid. Entries spanning
    // more cells than SAP_GRID_GIANT_CELL_SPAN on any axis (e.g. a large static
    // ground plane) go to giants_ instead of being inserted into every cell they
    // touch, keeping bucket count/memory bounded regardless of body size.
    {
        YOPE_PROF_SCOPE("sap_grid_bucket", "physics");
        const float cs = SAP_GRID_CELL_SIZE;
        cellEntries_.reserve(entries_.size());
        for (uint32_t i = 0; i < entries_.size(); ++i) {
            const Entry& en = entries_[i];
            // std::floor (not truncation) so negative world coordinates bucket
            // correctly — e.g. -0.25 must floor to cell -1, not truncate to 0.
            int minCx = (int)std::floor(en.minX / cs), maxCx = (int)std::floor(en.maxX / cs);
            int minCy = (int)std::floor(en.minY / cs), maxCy = (int)std::floor(en.maxY / cs);
            int minCz = (int)std::floor(en.minZ / cs), maxCz = (int)std::floor(en.maxZ / cs);
            if ((maxCx - minCx + 1) > SAP_GRID_GIANT_CELL_SPAN ||
                (maxCy - minCy + 1) > SAP_GRID_GIANT_CELL_SPAN ||
                (maxCz - minCz + 1) > SAP_GRID_GIANT_CELL_SPAN) {
                giants_.push_back(i);
                continue;
            }
            const uint32_t eid = en.entity.id;
            for (int cz = minCz; cz <= maxCz; ++cz)
                for (int cy = minCy; cy <= maxCy; ++cy)
                    for (int cx = minCx; cx <= maxCx; ++cx)
                        cellEntries_.push_back({ CellKey{cx, cy, cz}, i, eid });
        }
    }

    // Phase 3 — sort by cell, tie-broken by entity id. Contiguous equal-key runs
    // after this sort are the buckets (found by linear scan below) — no hash map,
    // same flat-array-over-unordered_map idiom IslandDetector uses for the same
    // determinism/perf reasons. Comparator reads only CellSlot's own fields (key,
    // entityId) — never entries_ — since dense grid scenarios put many bodies in
    // one cell, making key ties the common case, not the exception; an indirect
    // lookup on every tied comparison measured as the dominant low-N cost.
    {
        YOPE_PROF_SCOPE_N("sap_grid_sort", "physics", cellEntries_.size());
        std::sort(cellEntries_.begin(), cellEntries_.end(),
            [](const CellSlot& a, const CellSlot& b) {
                // Single-pass lexicographic compare — not "check == then
                // re-derive <", which redoes the same field comparisons twice.
                if (a.key.x != b.key.x) return a.key.x < b.key.x;
                if (a.key.y != b.key.y) return a.key.y < b.key.y;
                if (a.key.z != b.key.z) return a.key.z < b.key.z;
                return a.entityId < b.entityId;
            });
    }

    auto emit = [&](ecs::Entity a, ecs::Entity b) {
        if (a.id < b.id) out.emplace_back(a, b);
        else             out.emplace_back(b, a);
    };

    // Phase 4 — candidate pairs: within-bucket only. No neighbor-cell scan needed:
    // since every entry is inserted into the full Cartesian product of cells its
    // AABB spans (not just one "home" cell), any two truly-overlapping non-giant
    // entries are guaranteed to share at least one cell — for each axis, a point
    // in their overlap interval floors to a cell index common to both entries'
    // per-axis span, and insertion covers every combination of per-axis indices.
    // A neighbor scan would only be load-bearing for the "one home cell per
    // object" grid variant, which this isn't. Entries spanning multiple cells can
    // still produce the same candidate more than once (once per shared cell);
    // resolved by the sort+unique in Phase 6.
    {
        YOPE_PROF_SCOPE("sap_grid_query", "physics");
        const size_t M = cellEntries_.size();
        size_t i = 0;
        while (i < M) {
            size_t j = i;
            while (j < M && cellEntries_[j].key == cellEntries_[i].key) ++j;

            for (size_t a = i; a < j; ++a) {
                const Entry& ea = entries_[cellEntries_[a].entryIndex];
                for (size_t b = a + 1; b < j; ++b) {
                    const Entry& eb = entries_[cellEntries_[b].entryIndex];
                    if (overlaps3D(ea, eb)) emit(ea.entity, eb.entity);
                }
            }
            i = j;
        }
    }

    // Phase 5 — giants: tested against every non-giant entry, and pairwise
    // against each other. Expected rare (ground planes, a few large statics).
    if (!giants_.empty()) {
        YOPE_PROF_SCOPE("sap_giants", "physics");
        isGiant_.assign(entries_.size(), 0);
        for (uint32_t gi : giants_) isGiant_[gi] = 1;

        for (size_t gi = 0; gi < giants_.size(); ++gi) {
            const Entry& ga = entries_[giants_[gi]];
            for (uint32_t j = 0; j < entries_.size(); ++j) {
                if (isGiant_[j]) continue;
                const Entry& eb = entries_[j];
                if (overlaps3D(ga, eb)) emit(ga.entity, eb.entity);
            }
            for (size_t gj = gi + 1; gj < giants_.size(); ++gj) {
                const Entry& gb = entries_[giants_[gj]];
                if (overlaps3D(ga, gb)) emit(ga.entity, gb.entity);
            }
        }
    }

    // Phase 6 — canonicalize + dedupe + sort. Handles multi-cell duplicate
    // candidates and makes output order deterministic (by entity id), independent
    // of cell-processing order or the input entities order.
    {
        YOPE_PROF_SCOPE("sap_finalize", "physics");
        std::sort(out.begin(), out.end(),
            [](const std::pair<ecs::Entity, ecs::Entity>& p1, const std::pair<ecs::Entity, ecs::Entity>& p2) {
                if (p1.first.id != p2.first.id) return p1.first.id < p2.first.id;
                return p1.second.id < p2.second.id;
            });
        out.erase(std::unique(out.begin(), out.end(),
            [](const std::pair<ecs::Entity, ecs::Entity>& p1, const std::pair<ecs::Entity, ecs::Entity>& p2) {
                return p1.first.id == p2.first.id && p1.second.id == p2.second.id;
            }), out.end());
    }
}

} // namespace physics
