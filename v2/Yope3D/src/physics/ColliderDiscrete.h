#pragma once
#include <vector>
#include "ColliderGeom.h"
#include "ColliderTypes.h"
#include "ColliderSupports.h"
#include "ColliderAnalytical.h"
#include "ColliderGJK.h"
#include "ContactCache.h"
#include "../ecs/Entity.h"

namespace ecs { class Registry; }

namespace physics {

namespace ColliderDiscrete {

    // ECS-based detect — used by advance().
    void detect(ecs::Entity ea, ecs::Entity eb, ecs::Registry& reg,
                std::vector<ActiveContact>& contacts);

    // ECS-based solve — used by advance().
    void solveIsland(std::vector<ActiveContact>& contacts, float dt,
                     ecs::Registry& reg, EntityContactCache& cache);

    // Per-shape-pair narrowphase timing (Phase E profiler — A4).
    // Call reset() before the narrowphase loop and emit() after; detect()
    // accumulates per-pair-type µs/counts in between. Each emit() pushes 6
    // records (nphase_<a>_<b>) into the profiler stream with scope_n = pair
    // count for that bucket. No-ops in NDEBUG.
    void resetNarrowphaseTiming();
    void emitNarrowphaseProfile();

} // namespace ColliderDiscrete
} // namespace physics
