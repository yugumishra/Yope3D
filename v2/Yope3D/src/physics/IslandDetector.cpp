#include "IslandDetector.h"
#include "../ecs/Registry.h"
#include "../ecs/Components.h"
#include "../debug/Profiler.h"
#include <unordered_map>
#include <numeric>
#include <cstdint>
#include <algorithm>

namespace physics {

// Sentinel for "no UF id assigned" in entityToUfId_.
static constexpr int kNoUfId = -1;

void IslandDetector::build(
    const std::vector<ColliderDiscrete::ActiveContact>& allContacts,
    const EntityContactCache& globalCache,
    std::vector<Island>& islands,
    ecs::Registry& reg,
    const std::vector<std::pair<ecs::Entity, ecs::Entity>>& springPairs,
    const std::vector<std::pair<ecs::Entity, ecs::Entity>>& jointPairs,
    const std::vector<Joint*>& allJoints)
{
    islands.clear();
    // A connected component with zero geometric contacts (e.g. a ragdoll
    // hanging mid-air, joined only by joints/springs) must still get an
    // island — the old `allContacts.empty()` guard skipped island building
    // entirely in that case, silently dropping the joint/spring solve.
    if (allContacts.empty() && springPairs.empty() && jointPairs.empty()) return;

    auto isFixed = [&](ecs::Entity e) -> bool { return reg.has<ecs::Fixed>(e); };
    auto isSleeping = [&](ecs::Entity e) -> bool {
        auto* hc = reg.get<ecs::Hull>(e);
        return hc && hc->asleep;
    };
    auto wakeUp = [&](ecs::Entity e) {
        if (auto* hc = reg.get<ecs::Hull>(e)) { hc->asleep = false; hc->sleepFrames = 0; }
    };

    // 1. Assign union-find IDs to dynamic (non-fixed) entities only.
    //    Flat vector indexed by entity.id — entity IDs are dense integers
    //    handed out by the registry, so a direct array is 5–10× faster than
    //    the previous unordered_map<Entity, int> (the hash + probe was 19%
    //    of island_build at N=16k). Sentinel value kNoUfId = "not assigned".
    //    Requires one pre-pass to find max entity id (cheap — linear, no
    //    hashing). Generation is dropped from the key: contacts here are
    //    all from the current physics step so all generations are current.
    std::vector<int>& entityToUfId = entityToUfId_;  // reuse cross-step buffer
    int nextId = 0;
    {
        YOPE_PROF_SCOPE("island_id_assign", "physics");

        uint32_t maxEntityId = 0;
        for (const auto& c : allContacts) {
            if (!isFixed(c.a)) maxEntityId = std::max(maxEntityId, c.a.id);
            if (!isFixed(c.b)) maxEntityId = std::max(maxEntityId, c.b.id);
        }
        for (const auto& [ea, eb] : springPairs) {
            if (!isFixed(ea) && reg.has<ecs::Hull>(ea))
                maxEntityId = std::max(maxEntityId, ea.id);
            if (!isFixed(eb) && reg.has<ecs::Hull>(eb))
                maxEntityId = std::max(maxEntityId, eb.id);
        }
        for (const auto& [ea, eb] : jointPairs) {
            if (!isFixed(ea) && reg.has<ecs::Hull>(ea))
                maxEntityId = std::max(maxEntityId, ea.id);
            if (!isFixed(eb) && reg.has<ecs::Hull>(eb))
                maxEntityId = std::max(maxEntityId, eb.id);
        }

        // Grow once; shrink only if vastly over-sized (avoid reallocation churn).
        if (entityToUfId.size() < static_cast<size_t>(maxEntityId) + 1)
            entityToUfId.resize(maxEntityId + 1, kNoUfId);
        // Reset only the slots we'll actually touch. We don't know yet which
        // ones — but the working range is [0, maxEntityId], so reset that.
        // For dense entity IDs (the common case), this is the same cost as
        // resize-then-fill but reuses heap memory across calls.
        std::fill(entityToUfId.begin(),
                  entityToUfId.begin() + (maxEntityId + 1), kNoUfId);

        for (const auto& c : allContacts) {
            if (!isFixed(c.a) && entityToUfId[c.a.id] == kNoUfId)
                entityToUfId[c.a.id] = nextId++;
            if (!isFixed(c.b) && entityToUfId[c.b.id] == kNoUfId)
                entityToUfId[c.b.id] = nextId++;
        }
        // Spring endpoints may not appear in any contact — still assign IDs so
        // the union step can bridge their contact islands.
        for (const auto& [ea, eb] : springPairs) {
            if (!isFixed(ea) && reg.has<ecs::Hull>(ea)
                && entityToUfId[ea.id] == kNoUfId)
                entityToUfId[ea.id] = nextId++;
            if (!isFixed(eb) && reg.has<ecs::Hull>(eb)
                && entityToUfId[eb.id] == kNoUfId)
                entityToUfId[eb.id] = nextId++;
        }
        // Joint endpoints — same reasoning as spring endpoints above.
        for (const auto& [ea, eb] : jointPairs) {
            if (!isFixed(ea) && reg.has<ecs::Hull>(ea)
                && entityToUfId[ea.id] == kNoUfId)
                entityToUfId[ea.id] = nextId++;
            if (!isFixed(eb) && reg.has<ecs::Hull>(eb)
                && entityToUfId[eb.id] == kNoUfId)
                entityToUfId[eb.id] = nextId++;
        }
    }

    // 2. Union-find with path halving (iterative — no std::function).
    //    Path halving: in one pass, set parent[x] = parent[parent[x]] then
    //    advance. Asymptotically equivalent to two-pass full path compression
    //    but simpler and branch-light. Removes the type-erased call overhead
    //    of the previous std::function<int(int)> wrapper.
    parent_.assign(nextId, 0);
    std::iota(parent_.begin(), parent_.end(), 0);
    std::vector<int>& parent = parent_;  // local alias for the lambda capture

    auto find = [&parent](int x) -> int {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];   // halve
            x = parent[x];
        }
        return x;
    };

    {
        YOPE_PROF_SCOPE_N("island_unionfind", "physics", nextId);
        for (const auto& c : allContacts) {
            if (isFixed(c.a) || isFixed(c.b)) continue;
            int ra = find(entityToUfId[c.a.id]);
            int rb = find(entityToUfId[c.b.id]);
            if (ra != rb) parent[ra] = rb;
        }
        for (const auto& [ea, eb] : springPairs) {
            if (isFixed(ea) || isFixed(eb)) continue;
            if (ea.id >= entityToUfId.size() || eb.id >= entityToUfId.size()) continue;
            int ufA = entityToUfId[ea.id];
            int ufB = entityToUfId[eb.id];
            if (ufA == kNoUfId || ufB == kNoUfId) continue;
            int ra = find(ufA);
            int rb = find(ufB);
            if (ra != rb) parent[ra] = rb;
        }
        for (const auto& [ea, eb] : jointPairs) {
            if (isFixed(ea) || isFixed(eb)) continue;
            if (ea.id >= entityToUfId.size() || eb.id >= entityToUfId.size()) continue;
            int ufA = entityToUfId[ea.id];
            int ufB = entityToUfId[eb.id];
            if (ufA == kNoUfId || ufB == kNoUfId) continue;
            int ra = find(ufA);
            int rb = find(ufB);
            if (ra != rb) parent[ra] = rb;   // no-op self-pair (ea==eb) for single-body joints
        }
    }

    // 3. Partition contacts by island root.
    //    rootToIsland keys are dense UF ids (0..nextId-1) — replace the
    //    unordered_map<int,int> with a flat vector. Sentinel = -1 (no
    //    island created yet for that root).
    rootToIsland_.assign(nextId, -1);
    std::vector<int>& rootToIsland = rootToIsland_;
    {
        YOPE_PROF_SCOPE("island_partition", "physics");
        for (const auto& c : allContacts) {
            int root;
            if (!isFixed(c.a) && !isFixed(c.b))
                root = find(entityToUfId[c.a.id]);
            else if (!isFixed(c.a))
                root = find(entityToUfId[c.a.id]);
            else
                root = find(entityToUfId[c.b.id]);

            int islandIdx = rootToIsland[root];
            if (islandIdx < 0) {
                islandIdx = static_cast<int>(islands.size());
                rootToIsland[root] = islandIdx;
                islands.emplace_back();
            }
            islands[islandIdx].contacts.push_back(c);
        }
        // Seed an island for any spring/joint-connected component that has
        // zero geometric contacts (e.g. two bodies joined only by a joint,
        // never touching) — otherwise it would never get an Island entry and
        // would silently never be solved. Empty `contacts` is fine; solveIsland
        // already tolerates contacts.empty() with non-empty joints.
        auto seedIslandFor = [&](ecs::Entity ea, ecs::Entity eb) {
            if (isFixed(ea) && isFixed(eb)) return;
            ecs::Entity anchor = !isFixed(ea) ? ea : eb;
            if (anchor.id >= entityToUfId.size() || entityToUfId[anchor.id] == kNoUfId) return;
            int root = find(entityToUfId[anchor.id]);
            if (rootToIsland[root] < 0) {
                rootToIsland[root] = static_cast<int>(islands.size());
                islands.emplace_back();
            }
        };
        for (const auto& [ea, eb] : springPairs) seedIslandFor(ea, eb);
        for (const auto& [ea, eb] : jointPairs)  seedIslandFor(ea, eb);
        YOPE_PROF_EMIT("island_partition_n", "physics", 0.0,
                       static_cast<int>(islands.size()));
    }

    // 4. Per island: collect unique entities + warm-start cache snapshot.
    //    Hoisted seen-set: one shared vector<uint8_t> keyed by entity.id
    //    instead of allocating a fresh unordered_set<Entity> per island
    //    (~1300+ small allocations per step at N=16k). Reset only the
    //    entries we touched (per-island.entities) so cost stays O(island)
    //    not O(maxEntityId).
    if (seen_.size() < entityToUfId.size()) seen_.resize(entityToUfId.size(), 0);
    std::vector<uint8_t>& seen = seen_;
    {
        YOPE_PROF_SCOPE("island_entity_cache", "physics");

        // Attach each Joint* to the island owning its (anchor) root — must run
        // before the per-island contact loop below so joint-contributed entities
        // are already marked in `seen` and don't get double-pushed, and so a
        // joint-only island (seeded above with zero contacts) still ends up with
        // a populated entities list for Phase 5's wake propagation.
        for (Joint* jp : allJoints) {
            auto [ea, eb] = jointEntityPair(*jp);
            if (isFixed(ea) && isFixed(eb)) continue;
            ecs::Entity anchor = !isFixed(ea) ? ea : eb;
            if (anchor.id >= entityToUfId.size() || entityToUfId[anchor.id] == kNoUfId) continue;
            int root = find(entityToUfId[anchor.id]);
            int islandIdx = rootToIsland[root];
            if (islandIdx < 0) continue;   // shouldn't happen given the seeding pass above
            Island& isl = islands[islandIdx];
            isl.joints.push_back(jp);
            if (ea.id < seen.size() && !seen[ea.id]) { seen[ea.id] = 1; isl.entities.push_back(ea); }
            if (eb.id < seen.size() && !seen[eb.id]) { seen[eb.id] = 1; isl.entities.push_back(eb); }
        }

        int totalEntities = 0;
        for (int islandIdx = 0; islandIdx < static_cast<int>(islands.size()); ++islandIdx) {
            Island& isl = islands[islandIdx];
            for (const auto& c : isl.contacts) {
                if (!seen[c.a.id]) { seen[c.a.id] = 1; isl.entities.push_back(c.a); }
                if (!seen[c.b.id]) { seen[c.b.id] = 1; isl.entities.push_back(c.b); }
                for (int i = 0; i < c.manifold.numContacts; ++i) {
                    // Key must match solveIsland's exactly — the old {a, b, i}
                    // snapshot never carried compound sub-shape entries
                    // (shapeKey > 0), so those contacts silently lost warm-start.
                    EntityContactKey key{c.a, c.b, c.shapeKey, c.manifold.featureIds[i]};
                    auto it = globalCache.find(key);
                    if (it != globalCache.end())
                        isl.localCache[key] = it->second;
                }
            }
            // Reset only the slots this island marked. Entities are unique
            // to one island (UF partition guarantees this), so no risk of
            // clearing another island's bits.
            for (ecs::Entity e : isl.entities) seen[e.id] = 0;
            totalEntities += static_cast<int>(isl.entities.size());
        }
        YOPE_PROF_EMIT("island_entity_cache_n", "physics", 0.0, totalEntities);
    }

    // 5. Wake propagation: one awake entity wakes the entire island.
    {
        YOPE_PROF_SCOPE("island_wake", "physics");
        for (auto& isl : islands) {
            bool anyAwake = false;
            for (ecs::Entity e : isl.entities)
                if (!isSleeping(e)) { anyAwake = true; break; }
            if (anyAwake)
                for (ecs::Entity e : isl.entities)
                    if (isSleeping(e)) wakeUp(e);
        }
    }
}

void IslandDetector::mergeCache(std::vector<Island>& islands, EntityContactCache& globalCache) {
    for (auto& isl : islands)
        for (auto& [key, val] : isl.localCache)
            globalCache[key] = val;
}

} // namespace physics
