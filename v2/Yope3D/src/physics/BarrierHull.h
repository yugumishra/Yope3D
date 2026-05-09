#pragma once
#include "Hull.h"
#include "Barrier.h"
#include "BoundedBarrier.h"
#include <vector>
#include <variant>

namespace physics {

class BarrierHull : public Hull {
public:
    BarrierHull(std::vector<std::variant<Barrier, BoundedBarrier>> barriers,
                math::Vec3 pos, math::Vec3 extent);

    const std::vector<std::variant<Barrier, BoundedBarrier>>& getBarriers() const {
        return barriers;
    }

    math::Vec3 getBroadExtent()              const override { return extent; }
    math::Mat3 genInverseInertiaTensor()     const override { return {}; }
    bool       inside(const math::Vec3&)     const override { return false; }

    // BarrierHull only participates in CCD barrier collision, not hull-hull PGS.
    void detect(Hull&)  override {}
    void collide(Hull&) override {}
    void detectCollision(CSphere&) override {}
    void detectCollision(CAABB&)   override {}
    void detectCollision(COBB&)    override {}
    void handleCollision(CSphere&) override {}
    void handleCollision(CAABB&)   override {}
    void handleCollision(COBB&)    override {}

    static BarrierHull genRectangularBarriers(math::Vec3 extent, math::Vec3 pos);

private:
    std::vector<std::variant<Barrier, BoundedBarrier>> barriers;
    math::Vec3 extent;
};

} // namespace physics
