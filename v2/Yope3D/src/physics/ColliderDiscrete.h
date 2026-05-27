#pragma once
#include <vector>
#include "../math/Vec3.h"
#include "../math/Mat3.h"
#include "ContactCache.h"
#include "../ecs/Entity.h"

namespace ecs { class Registry; }

namespace physics {
class Hull;
class CSphere;
class CAABB;
class COBB;

namespace ColliderDiscrete {

    struct ContactManifold {
        math::Vec3 normal;
        float      penetration  = 0.0f;
        float      depths[4]    = {};
        math::Vec3 contactPoints[4];
        int        numContacts  = 0;
    };

    // Shared precomputed fields for both HullActiveContact and ActiveContact.
#define CONTACT_PRECOMPUTED_FIELDS \
        ContactManifold manifold; \
        math::Vec3      T1, T2; \
        float           mu = 0.0f, e = 0.0f; \
        math::Mat3      IinvA, IinvB; \
        math::Vec3      rA[4], rB[4]; \
        float           W[4]    = {}; \
        float           Wt1[4]  = {}; \
        float           Wt2[4]  = {}; \
        float           neta[4] = {}; \
        float           lambda[4]   = {}; \
        float           lambdaT1[4] = {}; \
        float           lambdaT2[4] = {}; \
        float           lambdaP[4]  = {};

    // Legacy: Hull*-keyed contact — used by physics tests.
    struct HullActiveContact {
        Hull* a;
        Hull* b;
        CONTACT_PRECOMPUTED_FIELDS
    };

    // ECS: Entity-keyed contact — used by advance() (Phase D).
    struct ActiveContact {
        ecs::Entity a;
        ecs::Entity b;
        CONTACT_PRECOMPUTED_FIELDS
    };

#undef CONTACT_PRECOMPUTED_FIELDS

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

    // Low-level detection — geometry-struct based (no Hull dependency).
    bool detectSphereSphere(const SphereGeom& a, const SphereGeom& b, ContactManifold& m);
    bool detectSphereAABB  (const SphereGeom& a, const AABBGeom&   b, ContactManifold& m);
    bool detectSphereOBB   (const SphereGeom& a, const OBBGeom&    b, ContactManifold& m);
    bool detectAABBAABB    (const AABBGeom&   a, const AABBGeom&   b, ContactManifold& m);
    bool detectAABBOBB     (const AABBGeom&   a, const OBBGeom&    b, ContactManifold& m);
    bool detectOBBOBB      (const OBBGeom&    a, const OBBGeom&    b, ContactManifold& m);

    // Legacy Hull*-based detect — used by physics tests.
    void detect(Hull& a, Hull& b, std::vector<HullActiveContact>& contacts);

    // ECS-based detect — used by advance().
    void detect(ecs::Entity ea, ecs::Entity eb, ecs::Registry& reg,
                std::vector<ActiveContact>& contacts);

    // Legacy Hull*-based solve — used by physics tests.
    void solveIsland(std::vector<HullActiveContact>& contacts, float dt, ContactCache& cache);
    void solveAll   (std::vector<HullActiveContact>& contacts, float dt, ContactCache& cache);

    // ECS-based solve — used by advance(). Internally bridges via LegacyHullRef in Step 1.
    void solveIsland(std::vector<ActiveContact>& contacts, float dt,
                     ecs::Registry& reg, EntityContactCache& cache);

} // namespace ColliderDiscrete
} // namespace physics
