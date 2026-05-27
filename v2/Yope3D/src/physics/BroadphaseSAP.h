#pragma once
#include "Hull.h"
#include "../ecs/Entity.h"
#include <vector>
#include <memory>
#include <utility>

namespace ecs { class Registry; }

namespace physics {

// Sweep-and-Prune broadphase.
// Each call to collectPairs() sorts all tangible hulls/entities by AABB minX,
// sweeps for X-overlapping pairs, then filters by Y and Z overlap.
// Reuses internal buffers across frames — no per-frame heap allocation.
class BroadphaseSAP {
public:
    // Legacy Hull*-based overload — used by physics tests.
    void collectPairs(const std::vector<Hull*>& hulls,
                      std::vector<std::pair<Hull*, Hull*>>& out);

    // ECS overload — used by advance(). Reads positions and extents from Registry.
    void collectPairs(const std::vector<ecs::Entity>& entities,
                      ecs::Registry& reg,
                      std::vector<std::pair<ecs::Entity, ecs::Entity>>& out);

private:
    struct Entry {
        float minX, maxX;
        float minY, maxY;
        float minZ, maxZ;
        Hull* hull = nullptr;         // set for legacy path
        ecs::Entity entity = ecs::NullEntity; // set for ECS path
    };
    std::vector<Entry> entries_;
};

} // namespace physics
