#pragma once
#include "../math/Vec3.h"

namespace physics {

// Contact payload delivered alongside a collision enter event — the "weak
// contact data" gap (limitations.md §4.2) closed: callbacks used to receive
// only the entity pair, so impact-scaled sound/damage/particles were impossible.
//
// Populated from the deepest point of the solved manifold:
//   point   — world-space position of the deepest contact point.
//   normal  — the manifold normal, oriented from the event's `a` toward `b`
//             (a script dispatched as `b` should negate it if it wants an
//             outward-from-self normal).
//   impulse — the accumulated normal impulse across the manifold's points this
//             tick (Newton-seconds), i.e. the converged PGS lambda sum. A direct
//             proxy for impact strength.
//
// All fields are zero on an EXIT event (the pair has separated, so there is no
// contact) and impulse is zero for trigger overlaps (they skip the solver).
struct ContactInfo {
    math::Vec3 point   {};
    math::Vec3 normal  {};
    float      impulse = 0.0f;
};

} // namespace physics
