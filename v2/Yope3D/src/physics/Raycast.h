#pragma once
#include "../math/Vec3.h"
#include <limits>

namespace physics::Raycast {

// Returns t of first intersection; -1.f = miss.
// Does NOT reject negative t (origin inside sphere returns negative t).
[[nodiscard]] float raycastSphere(math::Vec3 ray, math::Vec3 start,
                                   math::Vec3 center, float radius);

// Returns smallest non-negative k; std::numeric_limits<float>::min() = miss.
[[nodiscard]] float raycastAABB(math::Vec3 ray, math::Vec3 start,
                                 math::Vec3 pos, math::Vec3 extent);

} // namespace physics::Raycast
