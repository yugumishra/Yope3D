#pragma once
#include "Hull.h"
#include "ColliderDiscrete.h"
#include "ContactCache.h"
#include <vector>

namespace physics {

struct Island {
    std::vector<ColliderDiscrete::ActiveContact> contacts;
    std::vector<Hull*>                           hulls;
    ContactCache                                 localCache;
};

// Partitions a flat contact list into connected-component islands using union-find.
// Thread-safe to solve: islands with disjoint hull sets never share data.
class IslandDetector {
public:
    // Populate `islands` from `allContacts`. Pre-fills each island's localCache
    // from globalCache for warm-starting. Applies wake propagation: if any hull
    // in an island is awake, all sleeping hulls in that island are woken.
    void build(const std::vector<ColliderDiscrete::ActiveContact>& allContacts,
               const ContactCache& globalCache,
               std::vector<Island>& islands);

    // After parallel solve, write each island's localCache back to globalCache.
    static void mergeCache(std::vector<Island>& islands, ContactCache& globalCache);
};

} // namespace physics
