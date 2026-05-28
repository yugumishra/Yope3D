#pragma once
#include "../ecs/Entity.h"
#include <vector>
#include <utility>

namespace ecs { class Registry; }

namespace physics {

// Sweep-and-Prune broadphase.
// Each call to collectPairs() sorts all tangible entities by AABB minX,
// sweeps for X-overlapping pairs, then filters by Y and Z overlap.
// Reuses internal buffers across frames — no per-frame heap allocation.
class BroadphaseSAP {
public:
    void collectPairs(const std::vector<ecs::Entity>& entities,
                      ecs::Registry& reg,
                      std::vector<std::pair<ecs::Entity, ecs::Entity>>& out);

private:
    struct Entry {
        float minX, maxX;
        float minY, maxY;
        float minZ, maxZ;
        ecs::Entity entity = ecs::NullEntity;
    };
    std::vector<Entry> entries_;
};

} // namespace physics
