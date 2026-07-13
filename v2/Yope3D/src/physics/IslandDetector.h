#pragma once
#include "ColliderDiscrete.h"
#include "ContactCache.h"
#include "Joint.h"
#include "../ecs/Entity.h"
#include <vector>

namespace ecs { class Registry; }

namespace physics {

// ECS island: Entity-keyed contacts, joints (pointers into World::joints_'s
// stable storage — see Joint.h), and entity list.
struct Island {
    std::vector<ColliderDiscrete::ActiveContact> contacts;
    std::vector<Joint*>                          joints;
    std::vector<ecs::Entity>                     entities;
    EntityContactCache                           localCache;
};

// Partitions a flat contact list into connected-component islands using union-find.
// Thread-safe to solve: islands with disjoint entity sets never share data.
class IslandDetector {
public:
    // Populate `islands` from `allContacts`. Pre-fills each island's localCache
    // from globalCache for warm-starting. Applies wake propagation via Registry.
    // springPairs: entity pairs from all active springs — ensures spring-connected
    // bodies are always merged into the same island even when not in contact.
    // jointPairs/allJoints: entity pairs + object pointers for all active joints —
    // like springPairs, ensures joint-connected bodies merge into one island even
    // with zero geometric contact (e.g. a ragdoll hanging in the air), AND (unlike
    // springs, which are solved globally outside islands) actually attaches each
    // Joint* to the island that will solve it in solveIsland().
    void build(const std::vector<ColliderDiscrete::ActiveContact>& allContacts,
               const EntityContactCache& globalCache,
               std::vector<Island>& islands,
               ecs::Registry& reg,
               const std::vector<std::pair<ecs::Entity, ecs::Entity>>& springPairs = {},
               const std::vector<std::pair<ecs::Entity, ecs::Entity>>& jointPairs = {},
               const std::vector<Joint*>& allJoints = {});

    // After parallel solve, write each island's localCache back to globalCache.
    static void mergeCache(std::vector<Island>& islands, EntityContactCache& globalCache);

private:
    // Persistent buffers — reused across build() calls to avoid per-step
    // heap allocation. Sized lazily; the previous unordered_map/set churn
    // accounted for ~50% of island_build wall time at N=16k.
    std::vector<int>     entityToUfId_;   // entity.id → UF id (kNoUfId = unset)
    std::vector<int>     parent_;          // UF parent array
    std::vector<int>     rootToIsland_;    // UF root → island index (-1 = unset)
    std::vector<uint8_t> seen_;            // entity.id → seen flag (phase 4 dedup)
};

} // namespace physics
