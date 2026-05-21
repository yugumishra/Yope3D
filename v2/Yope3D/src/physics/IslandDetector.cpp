#include "IslandDetector.h"
#include <unordered_map>
#include <unordered_set>
#include <numeric>
#include <functional>

namespace physics {

void IslandDetector::build(
    const std::vector<ColliderDiscrete::ActiveContact>& allContacts,
    const ContactCache& globalCache,
    std::vector<Island>& islands)
{
    islands.clear();
    if (allContacts.empty()) return;

    // 1. Assign IDs only to dynamic (non-fixed) hulls.
    //    Fixed bodies (floor, walls) are NOT union-find nodes — they don't
    //    propagate island membership. Without this, every object touching the
    //    floor merges into one giant island through the shared static body.
    std::unordered_map<Hull*, int> id;
    id.reserve(allContacts.size() * 2);
    int nextId = 0;
    for (const auto& c : allContacts) {
        if (!c.a->isFixed() && !id.count(c.a)) id[c.a] = nextId++;
        if (!c.b->isFixed() && !id.count(c.b)) id[c.b] = nextId++;
    }

    // 2. Union-find with path compression — only union dynamic pairs.
    std::vector<int> parent(nextId);
    std::iota(parent.begin(), parent.end(), 0);

    std::function<int(int)> find = [&](int x) -> int {
        if (parent[x] != x) parent[x] = find(parent[x]);
        return parent[x];
    };

    for (const auto& c : allContacts) {
        if (c.a->isFixed() || c.b->isFixed()) continue;
        int ra = find(id[c.a]);
        int rb = find(id[c.b]);
        if (ra != rb) parent[ra] = rb;
    }

    // 3. Map each root to an island index, partition contacts.
    //    Contacts with a fixed body belong to the island of the dynamic body.
    std::unordered_map<int, int> rootToIsland;
    rootToIsland.reserve(nextId / 2 + 1);
    for (const auto& c : allContacts) {
        int root;
        if (!c.a->isFixed() && !c.b->isFixed())
            root = find(id[c.a]);
        else if (!c.a->isFixed())
            root = find(id[c.a]);
        else
            root = find(id[c.b]);

        auto [it, inserted] = rootToIsland.emplace(root, (int)islands.size());
        if (inserted) islands.emplace_back();
        islands[it->second].contacts.push_back(c);
    }

    // 4. Per island: collect unique hulls + extract warm-start cache snapshot.
    for (auto& [root, idx] : rootToIsland) {
        Island& isl = islands[idx];
        std::unordered_set<Hull*> seen;
        seen.reserve(isl.contacts.size() * 2);
        for (const auto& c : isl.contacts) {
            if (seen.insert(c.a).second) isl.hulls.push_back(c.a);
            if (seen.insert(c.b).second) isl.hulls.push_back(c.b);
            for (int i = 0; i < c.manifold.numContacts; ++i) {
                auto it = globalCache.find({c.a, c.b, i});
                if (it != globalCache.end())
                    isl.localCache[{c.a, c.b, i}] = it->second;
            }
        }
    }

    // 5. Wake propagation: one awake hull wakes the entire island.
    for (auto& isl : islands) {
        bool anyAwake = false;
        for (Hull* h : isl.hulls)
            if (!h->isSleeping()) { anyAwake = true; break; }
        if (anyAwake)
            for (Hull* h : isl.hulls)
                if (h->isSleeping()) h->wakeUp();
    }
}

void IslandDetector::mergeCache(std::vector<Island>& islands, ContactCache& globalCache) {
    for (auto& isl : islands)
        for (auto& [key, val] : isl.localCache)
            globalCache[key] = val;
}

} // namespace physics
