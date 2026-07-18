#pragma once
#include "ColliderTypes.h"
#include <variant>

namespace ecs { class Registry; }

namespace physics {

namespace ColliderDiscrete {
    // Single-shape support: furthest point ON that one shape (world space) in the
    // given direction. This is the only per-shape math that needs to exist — every
    // pairwise CSO/witness support is built generically from these five via
    // support(A,B,dir) = supportSingle(A,dir) - supportSingle(B,-dir).
    math::Vec3 supportSphere  (const SphereGeom&   s, const math::Vec3& dir);
    math::Vec3 supportAABB    (const AABBGeom&     s, const math::Vec3& dir);
    math::Vec3 supportOBB     (const OBBGeom&      s, const math::Vec3& dir);
    math::Vec3 supportCapsule (const CapsuleGeom&  s, const math::Vec3& dir);
    math::Vec3 supportCylinder(const CylinderGeom& s, const math::Vec3& dir);

    // Overload-resolved wrapper so generic (visited) code can call one name
    // regardless of concrete shape type.
    inline math::Vec3 supportSingle(const SphereGeom&   s, const math::Vec3& dir) { return supportSphere(s, dir); }
    inline math::Vec3 supportSingle(const AABBGeom&     s, const math::Vec3& dir) { return supportAABB(s, dir); }
    inline math::Vec3 supportSingle(const OBBGeom&      s, const math::Vec3& dir) { return supportOBB(s, dir); }
    inline math::Vec3 supportSingle(const CapsuleGeom&  s, const math::Vec3& dir) { return supportCapsule(s, dir); }
    inline math::Vec3 supportSingle(const CylinderGeom& s, const math::Vec3& dir) { return supportCylinder(s, dir); }

    ShapeVariant makeShapeVariant(ecs::Entity& e, ecs::Registry& reg);
    math::Vec3   shapePosition(const ShapeVariant& v);

    // Support result carrying the CSO point AND the individual witness points on
    // each shape that produced it.
    struct SupportWitness {
        math::Vec3 cso; // point on the Minkowski difference A +- B (matches makeSupport())
        math::Vec3 onA; // witness point on shape A
        math::Vec3 onB; // witness point on shape B
    };

    // The dispatch — std::visit resolves both shapes' concrete types at compile
    // time (one of 25 instantiations), then support(A,B,dir) = supportSingle(A,dir)
    // - supportSingle(B,-dir) for every pair, no per-pair code needed. Deduced
    // (`auto`) return type: must stay defined here (not in the .cpp) so every
    // translation unit that calls it — ColliderDiscrete.cpp, ColliderGJK.cpp — sees
    // the definition, per the usual auto-return-type-across-TUs rule.
    inline auto makeSupport(const ShapeVariant& va, const ShapeVariant& vb) {
        return [&va, &vb](const math::Vec3& dir) -> math::Vec3 {
            return std::visit([&dir](const auto& a, const auto& b) {
                return supportSingle(a, dir) - supportSingle(b, -dir);
            }, va, vb);
        };
    }

    // Witness-aware sibling of makeSupport() — same std::visit dispatch (resolved
    // once per call, not per shape), same generic composition: onA/onB are just
    // supportSingle evaluated directly, no CSO-subtraction sign juggling required.
    // Deduced (`auto`) return type, so (like makeSupport) must stay defined here,
    // not in the .cpp.
    inline auto makeSupportWitness(const ShapeVariant& va, const ShapeVariant& vb) {
        return [&va, &vb](const math::Vec3& dir) -> SupportWitness {
            return std::visit([&dir](const auto& a, const auto& b) -> SupportWitness {
                math::Vec3 onA = supportSingle(a, dir);
                math::Vec3 onB = supportSingle(b, -dir);
                return {onA - onB, onA, onB};
            }, va, vb);
        };
    }
} // namespace ColliderDiscrete
} // namespace physics
