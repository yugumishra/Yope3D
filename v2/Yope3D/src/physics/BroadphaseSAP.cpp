#include "BroadphaseSAP.h"
#include "../ecs/Registry.h"
#include "../ecs/Components.h"
#include "../world/Transform.h"
#include <algorithm>
#include <cmath>

namespace physics {

void BroadphaseSAP::collectPairs(const std::vector<Hull*>& hulls,
                                  std::vector<std::pair<Hull*, Hull*>>& out)
{
    out.clear();
    entries_.clear();

    for (Hull* h : hulls) {
        if (!h->isTangible()) continue;
        math::Vec3 pos = h->getPosition();
        math::Vec3 ext = h->getBroadExtent();
        entries_.push_back({
            pos.x - ext.x, pos.x + ext.x,
            pos.y - ext.y, pos.y + ext.y,
            pos.z - ext.z, pos.z + ext.z,
            h
        });
    }

    std::sort(entries_.begin(), entries_.end(),
              [](const Entry& a, const Entry& b) { return a.minX < b.minX; });

    for (size_t i = 0; i < entries_.size(); ++i) {
        const Entry& a = entries_[i];
        for (size_t j = i + 1; j < entries_.size(); ++j) {
            const Entry& b = entries_[j];
            if (b.minX > a.maxX) break;
            if (b.minY > a.maxY || b.maxY < a.minY) continue;
            if (b.minZ > a.maxZ || b.maxZ < a.minZ) continue;
            out.emplace_back(a.hull, b.hull);
        }
    }
}


void BroadphaseSAP::collectPairs(const std::vector<ecs::Entity>& entities,
                                  ecs::Registry& reg,
                                  std::vector<std::pair<ecs::Entity, ecs::Entity>>& out)
{
    out.clear();
    entries_.clear();

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
            nullptr, e
        });
    }

    std::sort(entries_.begin(), entries_.end(),
              [](const Entry& a, const Entry& b) { return a.minX < b.minX; });

    for (size_t i = 0; i < entries_.size(); ++i) {
        const Entry& a = entries_[i];
        for (size_t j = i + 1; j < entries_.size(); ++j) {
            const Entry& b = entries_[j];
            if (b.minX > a.maxX) break;
            if (b.minY > a.maxY || b.maxY < a.minY) continue;
            if (b.minZ > a.maxZ || b.maxZ < a.minZ) continue;
            out.emplace_back(a.entity, b.entity);
        }
    }
}

} // namespace physics
