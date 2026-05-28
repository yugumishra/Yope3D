#include "IslandDetector.h"
#include "../ecs/Registry.h"
#include "../ecs/Components.h"
#include "../debug/Profiler.h"
#include <unordered_map>
#include <unordered_set>
#include <numeric>
#include <functional>

namespace physics {

void IslandDetector::build(
    const std::vector<ColliderDiscrete::ActiveContact>& allContacts,
    const EntityContactCache& globalCache,
    std::vector<Island>& islands,
    ecs::Registry& reg,
    const std::vector<std::pair<ecs::Entity, ecs::Entity>>& springPairs)
{
    islands.clear();
    if (allContacts.empty()) return;

    auto isFixed = [&](ecs::Entity e) -> bool { return reg.has<ecs::Fixed>(e); };
    auto isSleeping = [&](ecs::Entity e) -> bool { return reg.has<ecs::Sleeping>(e); };
    auto wakeUp = [&](ecs::Entity e) {
        if (reg.has<ecs::Sleeping>(e)) {
            reg.remove<ecs::Sleeping>(e);
            if (auto* hc = reg.get<ecs::Hull>(e))
                hc->sleepFrames = 0;
        }
    };

    // 1. Assign union-find IDs to dynamic (non-fixed) entities only.
    //    Include both contact endpoints and spring-connected entities so that
    //    spring pairs can bridge otherwise-disconnected contact islands.
    struct EntityHash {
        size_t operator()(ecs::Entity e) const {
            return std::hash<uint32_t>{}(e.id) ^ (std::hash<uint32_t>{}(e.generation) * 2654435761u);
        }
    };
    std::unordered_map<ecs::Entity, int, EntityHash> id;
    int nextId = 0;
    {
        // scope_n = number of unique dynamic entities entering union-find.
        YOPE_PROF_SCOPE("island_id_assign", "physics");
        id.reserve(allContacts.size() * 2 + springPairs.size() * 2);
        for (const auto& c : allContacts) {
            if (!isFixed(c.a) && !id.count(c.a)) id[c.a] = nextId++;
            if (!isFixed(c.b) && !id.count(c.b)) id[c.b] = nextId++;
        }
        // Spring endpoints may not appear in any contact — still assign IDs so the
        // union step can bridge their contact islands.
        for (const auto& [ea, eb] : springPairs) {
            if (!isFixed(ea) && reg.has<ecs::Hull>(ea) && !id.count(ea)) id[ea] = nextId++;
            if (!isFixed(eb) && reg.has<ecs::Hull>(eb) && !id.count(eb)) id[eb] = nextId++;
        }
    }

    // 2. Union-find with path compression.
    // NOTE: std::function<int(int)> wraps a recursive lambda — every find()
    // call pays a type-erased indirect call. If island_unionfind dominates
    // island_build, this is the most likely culprit (not the algorithm).
    std::vector<int> parent(nextId);
    std::iota(parent.begin(), parent.end(), 0);

    std::function<int(int)> find = [&](int x) -> int {
        if (parent[x] != x) parent[x] = find(parent[x]);
        return parent[x];
    };

    {
        YOPE_PROF_SCOPE_N("island_unionfind", "physics", nextId);
        for (const auto& c : allContacts) {
            if (isFixed(c.a) || isFixed(c.b)) continue;
            int ra = find(id[c.a]);
            int rb = find(id[c.b]);
            if (ra != rb) parent[ra] = rb;
        }
        // Union spring-connected pairs so their contact islands are merged.
        for (const auto& [ea, eb] : springPairs) {
            auto itA = id.find(ea);
            auto itB = id.find(eb);
            if (itA == id.end() || itB == id.end()) continue;
            int ra = find(itA->second);
            int rb = find(itB->second);
            if (ra != rb) parent[ra] = rb;
        }
    }

    // 3. Partition contacts by island root.
    std::unordered_map<int, int> rootToIsland;
    {
        // scope_n = final island count (after dedup by root).
        YOPE_PROF_SCOPE("island_partition", "physics");
        rootToIsland.reserve(nextId / 2 + 1);
        for (const auto& c : allContacts) {
            int root;
            if (!isFixed(c.a) && !isFixed(c.b))
                root = find(id[c.a]);
            else if (!isFixed(c.a))
                root = find(id[c.a]);
            else
                root = find(id[c.b]);

            auto [it, inserted] = rootToIsland.emplace(root, (int)islands.size());
            if (inserted) islands.emplace_back();
            islands[it->second].contacts.push_back(c);
        }
        // stamp island count after partition is done; SCOPE_N captures at
        // construction so we'd see 0. Emit a follow-up zero-duration record
        // tagged with the final island count for the analyzer.
        YOPE_PROF_EMIT("island_partition_n", "physics", 0.0,
                       static_cast<int>(islands.size()));
    }

    // 4. Per island: collect unique entities + warm-start cache snapshot.
    {
        // scope_n = total unique entities across all islands (approx — counted
        // by summing each island's final entity count after dedup).
        YOPE_PROF_SCOPE("island_entity_cache", "physics");
        int totalEntities = 0;
        for (auto& [root, idx] : rootToIsland) {
            Island& isl = islands[idx];
            std::unordered_set<ecs::Entity, EntityHash> seen;
            seen.reserve(isl.contacts.size() * 2);
            for (const auto& c : isl.contacts) {
                if (seen.insert(c.a).second) isl.entities.push_back(c.a);
                if (seen.insert(c.b).second) isl.entities.push_back(c.b);
                for (int i = 0; i < c.manifold.numContacts; ++i) {
                    auto it = globalCache.find({c.a, c.b, i});
                    if (it != globalCache.end())
                        isl.localCache[{c.a, c.b, i}] = it->second;
                }
            }
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
