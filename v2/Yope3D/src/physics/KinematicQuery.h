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

// Result of a thin-ray query. `entity` identifies what was struck (the win over
// capsuleCast, which only returns t/normal) — enables click-to-pick / shoot-target.
struct RayHit {
    bool        hit    = false;
    ecs::Entity entity = ecs::NullEntity;
    math::Vec3  point  {};   // world-space contact point (origin + dir*t)
    math::Vec3  normal {};   // surface normal at the hit
    float       t      = 0.f; // distance along the (normalized) ray
};

// Cast a thin ray from `origin` along `dir` (need not be normalized; `t` is
// reported in normalized units, i.e. world meters). Returns the nearest tangible
// hit within `maxDist`, or {hit=false} on a miss. Skips `exclude`.
// Coverage: sphere / AABB / OBB bodies (matches capsuleCast). Capsule and
// cylinder obstacles are not yet ray-tested.
RayHit raycast(math::Vec3 origin, math::Vec3 dir, float maxDist,
               ecs::Registry& reg, ecs::Entity exclude);

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
