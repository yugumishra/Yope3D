#include "ColliderDiscrete.h"

namespace physics::ColliderDiscrete {

bool detectSphereSphere(const CSphere&, const CSphere&, ContactManifold&) { return false; }
bool detectSphereAABB  (const CSphere&, const CAABB&,   ContactManifold&) { return false; }
bool detectSphereOBB   (const CSphere&, const COBB&,    ContactManifold&) { return false; }
bool detectAABBAABB    (const CAABB&,   const CAABB&,   ContactManifold&) { return false; }
bool detectAABBOBB     (const CAABB&,   const COBB&,    ContactManifold&) { return false; }
bool detectOBBOBB      (const COBB&,    const COBB&,    ContactManifold&) { return false; }

void collide(Hull& /*a*/, Hull& /*b*/, float /*dt*/) {
    // TODO: implement double-dispatch + pgsCollisionResponse
    // See milestone6plan.md for full bug-fixed implementation.
}

} // namespace physics::ColliderDiscrete
