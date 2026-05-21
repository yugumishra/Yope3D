#include "BroadphaseSAP.h"
#include <algorithm>
#include <cmath>

namespace physics {

void BroadphaseSAP::collectPairs(const std::vector<std::unique_ptr<Hull>>& hulls,
                                  std::vector<std::pair<Hull*, Hull*>>& out)
{
    out.clear();
    entries_.clear();

    for (const auto& h : hulls) {
        if (!h->isTangible()) continue;
        math::Vec3 pos = h->getPosition();
        math::Vec3 ext = h->getBroadExtent();
        entries_.push_back({
            pos.x - ext.x, pos.x + ext.x,
            pos.y - ext.y, pos.y + ext.y,
            pos.z - ext.z, pos.z + ext.z,
            h.get()
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

} // namespace physics
