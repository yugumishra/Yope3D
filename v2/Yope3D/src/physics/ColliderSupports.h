#pragma once
#include "ColliderTypes.h"
#include <variant>

namespace ecs { class Registry; }

namespace physics {

namespace ColliderDiscrete {
    //support functions for GJK implementation (done pairwise to optimize support calculations)
    //computes furthest point in the direction specified on the minkowski difference defined by shape a - shape b
    math::Vec3 supportSphereSphere  (const SphereGeom&   a, const SphereGeom&   b, const math::Vec3& dir);
    math::Vec3 supportSphereAABB    (const SphereGeom&   a, const AABBGeom&     b, const math::Vec3& dir);
    math::Vec3 supportSphereOBB     (const SphereGeom&   a, const OBBGeom&      b, const math::Vec3& dir);
    math::Vec3 supportAABBAABB      (const AABBGeom&     a, const AABBGeom&     b, const math::Vec3& dir);
    math::Vec3 supportAABBOBB       (const AABBGeom&     a, const OBBGeom&      b, const math::Vec3& dir);
    math::Vec3 supportOBBOBB        (const OBBGeom&      a, const OBBGeom&      b, const math::Vec3& dir);
    // New per-pair stubs — bodies left empty; user implements support math.
    math::Vec3 supportSphereCapsule  (const SphereGeom&   a, const CapsuleGeom&  b, const math::Vec3& dir);
    math::Vec3 supportSphereCylinder (const SphereGeom&   a, const CylinderGeom& b, const math::Vec3& dir);
    math::Vec3 supportAABBCapsule    (const AABBGeom&     a, const CapsuleGeom&  b, const math::Vec3& dir);
    math::Vec3 supportAABBCylinder   (const AABBGeom&     a, const CylinderGeom& b, const math::Vec3& dir);
    math::Vec3 supportOBBCapsule     (const OBBGeom&      a, const CapsuleGeom&  b, const math::Vec3& dir);
    math::Vec3 supportOBBCylinder    (const OBBGeom&      a, const CylinderGeom& b, const math::Vec3& dir);
    math::Vec3 supportCapsuleCapsule (const CapsuleGeom&  a, const CapsuleGeom&  b, const math::Vec3& dir);
    math::Vec3 supportCapsuleCylinder(const CapsuleGeom&  a, const CylinderGeom& b, const math::Vec3& dir);
    math::Vec3 supportCylinderCylinder(const CylinderGeom& a, const CylinderGeom& b, const math::Vec3& dir);

    // Unified overload set — lets std::visit dispatch by type automatically (25 ordered combos for 5 shapes)
    math::Vec3 support(const SphereGeom&   a, const SphereGeom&   b, const math::Vec3& d);
    math::Vec3 support(const SphereGeom&   a, const AABBGeom&     b, const math::Vec3& d);
    math::Vec3 support(const SphereGeom&   a, const OBBGeom&      b, const math::Vec3& d);
    math::Vec3 support(const AABBGeom&     a, const SphereGeom&   b, const math::Vec3& d);
    math::Vec3 support(const AABBGeom&     a, const AABBGeom&     b, const math::Vec3& d);
    math::Vec3 support(const AABBGeom&     a, const OBBGeom&      b, const math::Vec3& d);
    math::Vec3 support(const OBBGeom&      a, const SphereGeom&   b, const math::Vec3& d);
    math::Vec3 support(const OBBGeom&      a, const AABBGeom&     b, const math::Vec3& d);
    math::Vec3 support(const OBBGeom&      a, const OBBGeom&      b, const math::Vec3& d);
    // Capsule overloads
    math::Vec3 support(const SphereGeom&   a, const CapsuleGeom&  b, const math::Vec3& d);
    math::Vec3 support(const CapsuleGeom&  a, const SphereGeom&   b, const math::Vec3& d);
    math::Vec3 support(const AABBGeom&     a, const CapsuleGeom&  b, const math::Vec3& d);
    math::Vec3 support(const CapsuleGeom&  a, const AABBGeom&     b, const math::Vec3& d);
    math::Vec3 support(const OBBGeom&      a, const CapsuleGeom&  b, const math::Vec3& d);
    math::Vec3 support(const CapsuleGeom&  a, const OBBGeom&      b, const math::Vec3& d);
    math::Vec3 support(const CapsuleGeom&  a, const CapsuleGeom&  b, const math::Vec3& d);
    // Cylinder overloads
    math::Vec3 support(const SphereGeom&   a, const CylinderGeom& b, const math::Vec3& d);
    math::Vec3 support(const CylinderGeom& a, const SphereGeom&   b, const math::Vec3& d);
    math::Vec3 support(const AABBGeom&     a, const CylinderGeom& b, const math::Vec3& d);
    math::Vec3 support(const CylinderGeom& a, const AABBGeom&     b, const math::Vec3& d);
    math::Vec3 support(const OBBGeom&      a, const CylinderGeom& b, const math::Vec3& d);
    math::Vec3 support(const CylinderGeom& a, const OBBGeom&      b, const math::Vec3& d);
    math::Vec3 support(const CapsuleGeom&  a, const CylinderGeom& b, const math::Vec3& d);
    math::Vec3 support(const CylinderGeom& a, const CapsuleGeom&  b, const math::Vec3& d);
    math::Vec3 support(const CylinderGeom& a, const CylinderGeom& b, const math::Vec3& d);

    ShapeVariant makeShapeVariant(ecs::Entity& e, ecs::Registry& reg);
    math::Vec3   shapePosition(const ShapeVariant& v);

    // The dispatch — std::visit resolves at compile time to one of 25 instantiations.
    // Deduced (`auto`) return type: must stay defined here (not in the .cpp) so every
    // translation unit that calls it — ColliderDiscrete.cpp, ColliderGJK.cpp — sees
    // the definition, per the usual auto-return-type-across-TUs rule.
    inline auto makeSupport(const ShapeVariant& va, const ShapeVariant& vb) {
        return [&va, &vb](const math::Vec3& dir) -> math::Vec3 {
            return std::visit([&dir](const auto& a, const auto& b) {
                return support(a, b, dir);
            }, va, vb);
        };
    }

    // ========================================================================
    // Distance-mode support: witness-aware pairwise overload set.
    //
    // GJK distance mode (gjkDistance, ColliderGJK.h) needs to recover WHERE on each
    // shape the closest points are, not just the CSO point — but the 25 support()
    // overloads above compute onA/onB internally and discard them (gjkIntersect's
    // boolean mode never needed them). This mirrors that same 25-overload,
    // resolve-once-via-std::visit structure exactly (not a generic single-shape
    // decomposition) so each pair keeps its hand-tuned algebra and shared
    // subexpressions between the onA/onB terms — user-implemented, one pair at a time.
    // ========================================================================

    // Support result carrying the CSO point AND the individual witness points on
    // each shape that produced it.
    struct SupportWitness {
        math::Vec3 cso; // point on the Minkowski difference A ⊖ B (matches support())
        math::Vec3 onA; // witness point on shape A
        math::Vec3 onB; // witness point on shape B
    };

    // Stubs — bodies left empty in the .cpp; user implements per pair (reusing/
    // adapting the corresponding support*() math above to also track onA/onB).
    SupportWitness supportWithWitness(const SphereGeom&   a, const SphereGeom&   b, const math::Vec3& d);
    SupportWitness supportWithWitness(const SphereGeom&   a, const AABBGeom&     b, const math::Vec3& d);
    SupportWitness supportWithWitness(const SphereGeom&   a, const OBBGeom&      b, const math::Vec3& d);
    SupportWitness supportWithWitness(const AABBGeom&     a, const SphereGeom&   b, const math::Vec3& d);
    SupportWitness supportWithWitness(const AABBGeom&     a, const AABBGeom&     b, const math::Vec3& d);
    SupportWitness supportWithWitness(const AABBGeom&     a, const OBBGeom&      b, const math::Vec3& d);
    SupportWitness supportWithWitness(const OBBGeom&      a, const SphereGeom&   b, const math::Vec3& d);
    SupportWitness supportWithWitness(const OBBGeom&      a, const AABBGeom&     b, const math::Vec3& d);
    SupportWitness supportWithWitness(const OBBGeom&      a, const OBBGeom&      b, const math::Vec3& d);
    // Capsule overloads
    SupportWitness supportWithWitness(const SphereGeom&   a, const CapsuleGeom&  b, const math::Vec3& d);
    SupportWitness supportWithWitness(const CapsuleGeom&  a, const SphereGeom&   b, const math::Vec3& d);
    SupportWitness supportWithWitness(const AABBGeom&     a, const CapsuleGeom&  b, const math::Vec3& d);
    SupportWitness supportWithWitness(const CapsuleGeom&  a, const AABBGeom&     b, const math::Vec3& d);
    SupportWitness supportWithWitness(const OBBGeom&      a, const CapsuleGeom&  b, const math::Vec3& d);
    SupportWitness supportWithWitness(const CapsuleGeom&  a, const OBBGeom&      b, const math::Vec3& d);
    SupportWitness supportWithWitness(const CapsuleGeom&  a, const CapsuleGeom&  b, const math::Vec3& d);
    // Cylinder overloads
    SupportWitness supportWithWitness(const SphereGeom&   a, const CylinderGeom& b, const math::Vec3& d);
    SupportWitness supportWithWitness(const CylinderGeom& a, const SphereGeom&   b, const math::Vec3& d);
    SupportWitness supportWithWitness(const AABBGeom&     a, const CylinderGeom& b, const math::Vec3& d);
    SupportWitness supportWithWitness(const CylinderGeom& a, const AABBGeom&     b, const math::Vec3& d);
    SupportWitness supportWithWitness(const OBBGeom&      a, const CylinderGeom& b, const math::Vec3& d);
    SupportWitness supportWithWitness(const CylinderGeom& a, const OBBGeom&      b, const math::Vec3& d);
    SupportWitness supportWithWitness(const CapsuleGeom&  a, const CylinderGeom& b, const math::Vec3& d);
    SupportWitness supportWithWitness(const CylinderGeom& a, const CapsuleGeom&  b, const math::Vec3& d);
    SupportWitness supportWithWitness(const CylinderGeom& a, const CylinderGeom& b, const math::Vec3& d);

    // Witness-aware sibling of makeSupport() — same std::visit dispatch (resolved
    // once per call, not per shape). Deduced (`auto`) return type, so (like
    // makeSupport) must stay defined here, not in the .cpp.
    inline auto makeSupportWitness(const ShapeVariant& va, const ShapeVariant& vb) {
        return [&va, &vb](const math::Vec3& dir) -> SupportWitness {
            return std::visit([&dir](const auto& a, const auto& b) {
                return supportWithWitness(a, b, dir);
            }, va, vb);
        };
    }
} // namespace ColliderDiscrete
} // namespace physics
