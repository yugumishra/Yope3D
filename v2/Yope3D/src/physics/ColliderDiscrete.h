#pragma once
#include "../math/Vec3.h"

namespace physics {
class Hull;
class CSphere;
class CAABB;
class COBB;

namespace ColliderDiscrete {

    struct ContactManifold {
        math::Vec3 normal;
        float      penetration  = 0.0f;
        math::Vec3 contactPoints[4];
        int        numContacts  = 0;
    };

    // Detection — returns true and fills manifold if colliding.
    // TODO (Milestone 6b): implement — port from old_src_java/physics/Collider.java
    bool detectSphereSphere(const CSphere& a, const CSphere& b, ContactManifold& m);
    bool detectSphereAABB  (const CSphere& a, const CAABB&   b, ContactManifold& m);
    bool detectSphereOBB   (const CSphere& a, const COBB&    b, ContactManifold& m);
    bool detectAABBAABB    (const CAABB&   a, const CAABB&   b, ContactManifold& m);
    bool detectAABBOBB     (const CAABB&   a, const COBB&    b, ContactManifold& m);
    bool detectOBBOBB      (const COBB&    a, const COBB&    b, ContactManifold& m);

    // Primary entry: detect + PGS response if colliding.
    void collide(Hull& a, Hull& b, float dt);

} // namespace ColliderDiscrete
} // namespace physics
