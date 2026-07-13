#pragma once
#include "../math/Vec3.h"
#include <array>
#include <limits>

namespace physics::Raycast {

// Returns t of first intersection; -1.f = miss.
// Does NOT reject negative t (origin inside sphere returns negative t).
[[nodiscard]] float raycastSphere(math::Vec3 ray, math::Vec3 start,
                                   math::Vec3 center, float radius);

// Returns smallest non-negative k; std::numeric_limits<float>::min() = miss.
[[nodiscard]] float raycastAABB(math::Vec3 ray, math::Vec3 start,
                                 math::Vec3 pos, math::Vec3 extent);

// axes = world-space OBB axes (from COBB::getOBBAxes()).
// Returns tEnter (or tExit if origin is inside); std::numeric_limits<float>::min() = miss.
[[nodiscard]] float raycastOBB(math::Vec3 ray, math::Vec3 start,
                                math::Vec3 pos, math::Vec3 extent,
                                const std::array<math::Vec3, 3>& axes);

// Capsule = cylinder (radius, world-space `up` axis, half-height along it)
// capped by two hemispheres — segment endpoints are center ± up*halfHeight
// (matches CapsuleForm/CapsuleGeom's local +Y axis convention). `ray` must be
// normalized (unlike raycastSphere/AABB/OBB's `ray`, which is direction*maxDist
// scaled — capsule reuses raycastSphere internally for the end caps and that
// function's `t` is only physically meaningful when `ray` is unit length).
// Returns t of first intersection (>= 0); -1.f = miss.
[[nodiscard]] float raycastCapsule(math::Vec3 ray, math::Vec3 start,
                                   math::Vec3 center, float radius,
                                   float halfHeight, math::Vec3 up);

} // namespace physics::Raycast
