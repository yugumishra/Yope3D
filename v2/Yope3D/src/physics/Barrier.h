#pragma once
#include "../math/Vec3.h"

namespace physics {

struct Barrier {
    math::Vec3 normal;
    math::Vec3 position;

    Barrier(math::Vec3 n, math::Vec3 p) : normal(n), position(p) {}
    virtual ~Barrier() = default;
};

} // namespace physics
