#pragma once
#include <vector>
#include "../math/Vec3.h"
#include "../math/Mat3.h"
#include "ContactCache.h"

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

    // Holds one contact pair's full state for the global PGS solve.
    struct ActiveContact {
        Hull*           a;
        Hull*           b;
        ContactManifold manifold;
        // Precomputed (filled by solveAll before iteration)
        math::Vec3      T1, T2;
        float           mu = 0.0f, e = 0.0f;
        math::Mat3      IinvA, IinvB;
        math::Vec3      rA[4], rB[4];
        float           W[4]    = {};
        float           Wt1[4]  = {};
        float           Wt2[4]  = {};
        float           neta[4] = {};
        // Accumulated lambdas
        float           lambda[4]   = {};
        float           lambdaT1[4] = {};
        float           lambdaT2[4] = {};
        float           lambdaP[4]  = {};
    };

    // Low-level detection — return true and fill m if colliding.
    bool detectSphereSphere(const CSphere& a, const CSphere& b, ContactManifold& m);
    bool detectSphereAABB  (const CSphere& a, const CAABB&   b, ContactManifold& m);
    bool detectSphereOBB   (const CSphere& a, const COBB&    b, ContactManifold& m);
    bool detectAABBAABB    (const CAABB&   a, const CAABB&   b, ContactManifold& m);
    bool detectAABBOBB     (const CAABB&   a, const COBB&    b, ContactManifold& m);
    bool detectOBBOBB      (const COBB&    a, const COBB&    b, ContactManifold& m);

    // Phase 1: detect one pair and append to contacts if colliding.
    void detect(Hull& a, Hull& b, std::vector<ActiveContact>& contacts);

    // Phase 2: global PGS solve over all detected contacts.
    void solveAll(std::vector<ActiveContact>& contacts, float dt, ContactCache& cache);

} // namespace ColliderDiscrete
} // namespace physics
