#include "BroadphaseSAP.h"
#include "../ecs/Registry.h"
#include "../ecs/Components.h"
#include "../world/Transform.h"
#include "../debug/Profiler.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace physics {

void BroadphaseSAP::collectPairs(const std::vector<ecs::Entity>& entities,
                                  ecs::Registry& reg,
                                  std::vector<std::pair<ecs::Entity, ecs::Entity>>& out)
{
    out.clear();
    entries_.clear();

    // Phase 1 — build entries from registry. Linear in N but each entity
    // pays 3–5 archetype lookups (Hull, Transform, plus one shape form).
    // If this dominates the broadphase cost, the registry get<>() path or
    // the entries_ vector growth is the target, not the sweep.
    {
        YOPE_PROF_SCOPE("sap_build", "physics");
        for (ecs::Entity e : entities) {
            auto* hc = reg.get<ecs::Hull>(e);
            if (!hc || !hc->tangible) continue;
            // Note: fixed entities are included so dynamic-vs-fixed pairs are generated.
            // Fixed-fixed pairs are filtered in advance() after broadphase.
            auto* tf = reg.get<Transform>(e);
            if (!tf) continue;
            math::Vec3 pos = tf->position;
            math::Vec3 ext{};
            if (auto* sf = reg.get<ecs::SphereForm>(e))
                ext = {sf->radius, sf->radius, sf->radius};
            else if (auto* af = reg.get<ecs::AABBForm>(e))
                ext = af->extent;
            else if (auto* of = reg.get<ecs::OBBForm>(e))
                ext = of->extent; // conservative AABB: use half-extents directly
            else
                continue; // no known shape
            entries_.push_back({
                pos.x - ext.x, pos.x + ext.x,
                pos.y - ext.y, pos.y + ext.y,
                pos.z - ext.z, pos.z + ext.z,
                e
            });
        }
    }

    // Phase 2 — sort by minX. O(N log N) compares.
    {
        YOPE_PROF_SCOPE("sap_sort", "physics");
        std::sort(entries_.begin(), entries_.end(),
                  [](const Entry& a, const Entry& b) { return a.minX < b.minX; });
    }

    // Phase 3 — sweep. Inner loop length depends on AABB density along X.
    // Stamped via emitRecord (not SCOPE_N) because we need the inner-iter
    // counter to be incremented during the loop and read at the end —
    // YOPE_PROF_SCOPE_N captures n at scope-construction time.
    // scope_n = total inner-loop iterations across all i. Plotting this
    // against N reveals whether sweep is O(N × density) or genuinely linear.
    {
        auto start = std::chrono::high_resolution_clock::now();
        int innerIters = 0;
        for (size_t i = 0; i < entries_.size(); ++i) {
            const Entry& a = entries_[i];
            for (size_t j = i + 1; j < entries_.size(); ++j) {
                ++innerIters;
                const Entry& b = entries_[j];
                if (b.minX > a.maxX) break;
                if (b.minY > a.maxY || b.maxY < a.minY) continue;
                if (b.minZ > a.maxZ || b.maxZ < a.minZ) continue;
                out.emplace_back(a.entity, b.entity);
            }
        }
        auto end = std::chrono::high_resolution_clock::now();
        double us = std::chrono::duration<double, std::micro>(end - start).count();
        YOPE_PROF_EMIT("sap_sweep", "physics", us, innerIters);
    }
}

} // namespace physics
