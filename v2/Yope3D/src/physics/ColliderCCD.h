#pragma once
#include "../math/Vec3.h"

namespace physics {
class Hull;
struct Barrier;
struct BoundedBarrier;

namespace ColliderCCD {
    void collideBarrier(Hull& one, const Barrier& b,        float dt, const math::Vec3& gravity);
    void collideBarrier(Hull& one, const BoundedBarrier& b, float dt, const math::Vec3& gravity);
} // namespace ColliderCCD
} // namespace physics
