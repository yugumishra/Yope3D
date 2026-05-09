#pragma once
#include "Barrier.h"

namespace physics {

struct BoundedBarrier : Barrier {
    float      xScale, yScale;
    math::Vec3 orientation; // first tangent direction

    BoundedBarrier(math::Vec3 n, math::Vec3 p, float xs, float ys, math::Vec3 orient)
        : Barrier(n, p), xScale(xs), yScale(ys), orientation(orient) {}

    math::Vec3 getSecondOrientation() const { return normal.cross(orientation); }
};

} // namespace physics
