#pragma once
#include "ColliderDiscrete.h"
#include "ContactCache.h"
#include "../ecs/Entity.h"
#include <vector>

namespace ecs { class Registry; }

namespace physics {

// ECS island: Entity-keyed contacts and entity list.
struct Island {
    std::vector<ColliderDiscrete::ActiveContact> contacts;
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
    void build(const std::vector<ColliderDiscrete::ActiveContact>& allContacts,
               const EntityContactCache& globalCache,
               std::vector<Island>& islands,
               ecs::Registry& reg,
               const std::vector<std::pair<ecs::Entity, ecs::Entity>>& springPairs = {});

    // After parallel solve, write each island's localCache back to globalCache.
    static void mergeCache(std::vector<Island>& islands, EntityContactCache& globalCache);
};

} // namespace physics
