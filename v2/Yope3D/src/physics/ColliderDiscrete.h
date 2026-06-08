#pragma once
#include <vector>
#include "../math/Vec3.h"
#include "../math/Mat3.h"
#include "ContactCache.h"
#include "../ecs/Entity.h"
#include <variant>
#include <functional>

namespace ecs { class Registry; }

namespace physics {

namespace ColliderDiscrete {

    struct ContactManifold {
        math::Vec3 normal;
        float      penetration  = 0.0f;
        float      depths[4]    = {};
        math::Vec3 contactPoints[4];
        int        numContacts  = 0;
    };

    // ECS: Entity-keyed contact — used by advance().
    struct ActiveContact {
        ecs::Entity a;
        ecs::Entity b;
        ContactManifold manifold;
        math::Vec3      T1, T2;
        float           mu = 0.0f, e = 0.0f;
        math::Mat3      IinvA, IinvB;
        math::Vec3      rA[4], rB[4];
        float           W[4]    = {};
        float           Wt1[4]  = {};
        float           Wt2[4]  = {};
        float           neta[4] = {};
        float           lambda[4]   = {};
        float           lambdaT1[4] = {};
        float           lambdaT2[4] = {};
        float           lambdaP[4]  = {};
    };

    // Lightweight geometry descriptors — avoid depending on Hull subclasses.
    struct SphereGeom {
        math::Vec3 pos;
        float      radius;
        math::Vec3 getPosition() const { return pos; }
        float      getRadius()   const { return radius; }
    };
    struct AABBGeom {
        math::Vec3 pos;
        math::Vec3 extent;
        math::Vec3 getPosition() const { return pos; }
        math::Vec3 getScales()   const { return extent; }
    };
    struct OBBGeom {
        math::Vec3 pos;
        math::Vec3 extent;
        math::Mat3 rot;
        math::Vec3 getPosition()     const { return pos; }
        math::Vec3 getScales()       const { return extent; }
        math::Mat3 getRotTransform() const { return rot; }
        std::array<math::Vec3,3> getOBBAxes() const {
            return {{ {rot.m[0],rot.m[1],rot.m[2]},
                      {rot.m[3],rot.m[4],rot.m[5]},
                      {rot.m[6],rot.m[7],rot.m[8]} }};
        }
        std::array<math::Vec3,8> worldSpaceCorners() const {
            auto ax = getOBBAxes();
            std::array<math::Vec3,8> c;
            for (int i = 0; i < 8; ++i) {
                float sx = (i&1)?-1.f:1.f, sy=(i&2)?-1.f:1.f, sz=(i&4)?-1.f:1.f;
                c[i] = pos + ax[0]*(sx*extent.x) + ax[1]*(sy*extent.y) + ax[2]*(sz*extent.z);
            }
            return c;
        }
        float projectOnto(math::Vec3 axis) const {
            auto ax = getOBBAxes();
            return extent.x*std::abs(ax[0].dot(axis))
                 + extent.y*std::abs(ax[1].dot(axis))
                 + extent.z*std::abs(ax[2].dot(axis));
        }
        bool inside(const math::Vec3& p) const {
            math::Mat3 Rt = rot.transpose();
            math::Vec3 local = Rt * (p - pos);
            return std::abs(local.x) <= extent.x
                && std::abs(local.y) <= extent.y
                && std::abs(local.z) <= extent.z;
        }
    };

    // Capsule: sphere-swept segment, local axis +Y.
    // The cylindrical body spans [-halfHeight, +halfHeight]; hemisphere caps at each end.
    struct CapsuleGeom {
        math::Vec3 pos;         // world-space center
        float      radius;
        float      halfHeight;  // half the cylinder section length
        math::Mat3 rot;         // rotation (local +Y → world axis)
        math::Vec3 getPosition()     const { return pos; }
        math::Mat3 getRotTransform() const { return rot; }
        float      getRadius()       const { return radius; }
        float      getHalfHeight()   const { return halfHeight; }
    };

    // Cylinder: disk-capped cylinder, local axis +Y.
    struct CylinderGeom {
        math::Vec3 pos;         // world-space center
        float      radius;
        float      halfHeight;
        math::Mat3 rot;         // rotation (local +Y → world axis)
        math::Vec3 getPosition()     const { return pos; }
        math::Mat3 getRotTransform() const { return rot; }
        float      getRadius()       const { return radius; }
        float      getHalfHeight()   const { return halfHeight; }
    }; 

    // Low-level detection — geometry-struct based (no Hull dependency).
    bool detectSphereSphere(const SphereGeom& a, const SphereGeom& b, ContactManifold& m);
    bool detectSphereAABB  (const SphereGeom& a, const AABBGeom&   b, ContactManifold& m);
    bool detectSphereOBB   (const SphereGeom& a, const OBBGeom&    b, ContactManifold& m);
    bool detectAABBAABB    (const AABBGeom&   a, const AABBGeom&   b, ContactManifold& m);
    bool detectAABBOBB     (const AABBGeom&   a, const OBBGeom&    b, ContactManifold& m);
    bool detectOBBOBB      (const OBBGeom&    a, const OBBGeom&    b, ContactManifold& m);

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

    //non virtual dispatch
    using ShapeVariant = std::variant<SphereGeom, AABBGeom, OBBGeom, CapsuleGeom, CylinderGeom>;
    ShapeVariant makeShapeVariant(ecs::Entity& e, ecs::Registry& reg);

    //struct returned by gjk that is the simplex it generates (used by epa)
    struct GJKSimplex {
        math::Vec3 points[4];
        int n = 0;
    };

    // One recorded iteration of gjkIntersect, for the editor stepper. Purely
    // observational — gjkIntersect only writes these when a trace pointer is passed,
    // so the stepper replays the REAL run instead of re-implementing the loop.
    struct GJKTraceFrame {
        math::Vec3 dir;        // search direction used this iteration (post guard)
        math::Vec3 support;    // support point returned for that direction
        float      dotSD;      // support·dir (the termination-test value)
        int        simplexN;   // simplex size after updateSimplex this iteration
        math::Vec3 pts[4];     // simplex vertices after updateSimplex
        bool       early;      // updateSimplex reported origin contained
        bool       terminated; // this frame is the "no intersection" early exit
    };
    using GJKTrace = std::vector<GJKTraceFrame>;

    // Zero-cost: templated GJK takes support by template param, inlined completely.
    // Optional trace: when non-null, each iteration appends a GJKTraceFrame.
    template<typename SupportFn>
    bool gjkIntersect(SupportFn&& support, GJKSimplex& simplex, math::Vec3 initDir,
                      GJKTrace* trace = nullptr);

    template<typename SupportFn>
    bool epaManifold(SupportFn&& support, GJKSimplex& simplex, ColliderDiscrete::ContactManifold& m);

    // The dispatch — std::visit resolves at compile time to one of 6 instantiations
    auto makeSupport(const ColliderDiscrete::ShapeVariant& va, const ColliderDiscrete::ShapeVariant& vb);
    math::Vec3 shapePosition(const ShapeVariant& v);

    //gjk detect
    void detectGJK(ecs::Entity ea, ecs::Entity eb, ecs::Registry& reg, std::vector<ActiveContact>& contacts);

    // ------------------------------------------------------------------------
    // GJK debug / test harness (editor oracle + simplex stepper).
    // Additive plumbing ONLY — none of this implements GJK; it just exposes the
    // existing pieces so editor-side tooling can drive the real algorithm.
    //   * updateSimplex      — fwd-decl of the existing (in-progress) Voronoi step,
    //                          so the stepper can advance it one iteration at a time.
    //   * makeSupportFn       — type-erased wrapper over makeSupport() (which returns
    //                          `auto`, hence uncallable across TUs); captures the two
    //                          shapes by value so the closure can outlive the call.
    //   * gjkBoolean          — runs the real templated gjkIntersect() on a pair and
    //                          reports intersection (+ optional final simplex).
    //   * satBoolean          — pure-geometry ground truth via the proven SAT
    //                          detect* routines (no layer/fixed/tangible gating), used
    //                          as the oracle to diff against gjkBoolean.
    // ------------------------------------------------------------------------
    bool updateSimplex(GJKSimplex& simplex, math::Vec3& direction);
    std::function<math::Vec3(math::Vec3)> makeSupportFn(ecs::Entity ea, ecs::Entity eb, ecs::Registry& reg);
    bool gjkBoolean(ecs::Entity ea, ecs::Entity eb, ecs::Registry& reg, GJKSimplex* outSimplex = nullptr);
    bool satBoolean(ecs::Entity ea, ecs::Entity eb, ecs::Registry& reg);
    // Runs the real gjkIntersect on a pair with tracing on; fills outTrace and
    // returns the intersection verdict. The stepper scrubs outTrace's frames.
    bool gjkTrace(ecs::Entity ea, ecs::Entity eb, ecs::Registry& reg, GJKTrace& outTrace);

    // ECS-based detect — used by advance().
    void detect(ecs::Entity ea, ecs::Entity eb, ecs::Registry& reg,
                std::vector<ActiveContact>& contacts);

    // ECS-based solve — used by advance().
    void solveIsland(std::vector<ActiveContact>& contacts, float dt,
                     ecs::Registry& reg, EntityContactCache& cache);

    // Per-shape-pair narrowphase timing (Phase E profiler — A4).
    // Call reset() before the narrowphase loop and emit() after; detect()
    // accumulates per-pair-type µs/counts in between. Each emit() pushes 6
    // records (nphase_<a>_<b>) into the profiler stream with scope_n = pair
    // count for that bucket. No-ops in NDEBUG.
    void resetNarrowphaseTiming();
    void emitNarrowphaseProfile();

} // namespace ColliderDiscrete
} // namespace physics
