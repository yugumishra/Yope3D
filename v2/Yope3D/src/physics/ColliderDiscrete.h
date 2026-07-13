#pragma once
#include <vector>
#include "ColliderGeom.h"
#include "ColliderTypes.h"
#include "ColliderSupports.h"
#include "ColliderAnalytical.h"
#include "ColliderGJK.h"
#include "ContactCache.h"
#include "Joint.h"
#include "../ecs/Entity.h"

namespace ecs { class Registry; }

namespace physics {

namespace ColliderDiscrete {

    // ECS-based detect — used by advance().
    void detect(ecs::Entity ea, ecs::Entity eb, ecs::Registry& reg,
                std::vector<ActiveContact>& contacts);

    // ECS-based solve — used by advance(). `joints` holds pointers into
    // World::joints_'s stable-address storage (not copies — unlike
    // ActiveContact, a Joint carries persistent warm-start state that must be
    // mutated in place and stay visible next frame). Solved interleaved with
    // contacts inside the same Gauss-Seidel iteration, not as a separate pass,
    // so e.g. a ragdoll joint feels the same iteration's ground contact impulse.
    void solveIsland(std::vector<ActiveContact>& contacts, std::vector<Joint*>& joints,
                     float dt, ecs::Registry& reg, EntityContactCache& cache);

    // Casts a SuspensionJoint's wheel ray and refreshes its grounded/hitPoint/
    // hitNormal/currentLength/worldUp fields. Must run BEFORE solveIsland (its
    // precompute pass only consumes these, it doesn't cast the ray itself) —
    // called once per substep in World::advance(), analogous to how narrowphase
    // detect() must run before the contacts it produces can be solved.
    void refreshSuspensionRaycast(SuspensionJoint& j, ecs::Registry& reg);

    // Per-shape-pair narrowphase timing (Phase E profiler — A4).
    // Call reset() before the narrowphase loop and emit() after; detect()
    // accumulates per-pair-type µs/counts in between. Each emit() pushes 6
    // records (nphase_<a>_<b>) into the profiler stream with scope_n = pair
    // count for that bucket. No-ops in NDEBUG.
    void resetNarrowphaseTiming();
    void emitNarrowphaseProfile();

} // namespace ColliderDiscrete
} // namespace physics
