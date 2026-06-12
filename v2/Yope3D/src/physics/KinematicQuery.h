#pragma once
#include "../math/Vec3.h"
#include "../ecs/Entity.h"
#include <vector>

namespace ecs { class Registry; }

namespace physics::KinematicQuery {

struct OverlapResult {
    math::Vec3 normal;  // direction to push the capsule out of the obstacle
    float      depth;
};

struct CastResult {
    float      t;       // distance along dir to first contact; maxDist if no hit
    bool       hit;
    math::Vec3 normal;
};

// Returns all overlapping contacts between an axis-aligned capsule (center `pos`,
// cylinder half-length `hh`, sphere radius `r`) and tangible world geometry.
// Tangible = has Fixed tag, OR has Hull with tangible==true. Skips `exclude`.
std::vector<OverlapResult> capsuleOverlap(
    math::Vec3 pos, float r, float hh,
    ecs::Registry& reg, ecs::Entity exclude);

// Ray-based cast from the capsule endpoint in `dir`. Accounts for sphere radius:
// returned t is the distance from the endpoint to the first surface contact.
// Primarily used for ground/ceiling detection (vertical dir).
CastResult capsuleCast(
    math::Vec3 pos, float r, float hh,
    math::Vec3 dir, float maxDist,
    ecs::Registry& reg, ecs::Entity exclude);

} // namespace physics::KinematicQuery
