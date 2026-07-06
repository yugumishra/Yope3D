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
} // namespace ColliderDiscrete
} // namespace physics
