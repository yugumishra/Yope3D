#pragma once
#include "ColliderTypes.h"

namespace ecs { class Registry; }

namespace physics {

namespace ColliderDiscrete {
    // Analytical/SAT detection — geometry-struct based (no Hull dependency).
    bool analyticalSphereSphere(const SphereGeom& a, const SphereGeom& b, ContactManifold& m);
    bool analyticalSphereAABB  (const SphereGeom& a, const AABBGeom&   b, ContactManifold& m);
    bool analyticalSphereOBB   (const SphereGeom& a, const OBBGeom&    b, ContactManifold& m);
    bool analyticalAABBAABB    (const AABBGeom&   a, const AABBGeom&   b, ContactManifold& m);
    bool analyticalAABBOBB     (const AABBGeom&   a, const OBBGeom&    b, ContactManifold& m);
    bool analyticalOBBOBB      (const OBBGeom&    a, const OBBGeom&    b, ContactManifold& m);

    // Analytical pair detection (sphere-capsule, capsule-capsule) — no live GJK
    // branch for these pairs yet, so they shunt to closed-form math instead.
    bool analyticalSphereCapsule (const SphereGeom&  a, const CapsuleGeom& b, ContactManifold& m);
    bool analyticalCapsuleCapsule(const CapsuleGeom& a, const CapsuleGeom& b, ContactManifold& m);

    // Returns true when this pair should bypass GJK and use analytical detection.
    // Pairs: (Sphere,Sphere)=(0,0), (Sphere,Capsule)=(0,3)/(3,0), (Capsule,Capsule)=(3,3),
    // plus all Sphere/AABB/OBB combinations (indices 0-2).
    bool isAnalyticalPair(const ShapeVariant& va, const ShapeVariant& vb);

    // std::visit dispatcher over isAnalyticalPair()'s pairs; unused combos return false.
    bool analyticalBoolean(const ShapeVariant& va, const ShapeVariant& vb, ContactManifold& m);

    // Pure-geometry SAT ground truth (no layer/fixed/tangible gating) — used as the
    // oracle the editor's GJKTestPanel diffs gjkBoolean() against.
    bool satBoolean(ecs::Entity ea, ecs::Entity eb, ecs::Registry& reg);
} // namespace ColliderDiscrete
} // namespace physics
