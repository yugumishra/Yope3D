#pragma once
#include "Hull.h"
#include <vector>
#include <memory>
#include <utility>

namespace physics {

// Sweep-and-Prune broadphase.
// Each call to collectPairs() sorts all tangible hulls by their AABB minX,
// sweeps for X-overlapping pairs, then filters by Y and Z overlap.
// Reuses internal buffers across frames — no per-frame heap allocation.
class BroadphaseSAP {
public:
    void collectPairs(const std::vector<Hull*>& hulls,
                      std::vector<std::pair<Hull*, Hull*>>& out);

private:
    struct Entry {
        float minX, maxX;
        float minY, maxY;
        float minZ, maxZ;
        Hull* hull;
    };
    std::vector<Entry> entries_;
};

} // namespace physics
