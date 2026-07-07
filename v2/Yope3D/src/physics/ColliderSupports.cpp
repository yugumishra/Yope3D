#include "ColliderSupports.h"
#include "../ecs/Registry.h"
#include "../ecs/Components.h"
#include "../world/Transform.h"
#include <cmath>

namespace physics {

namespace ColliderDiscrete {

// ============================================================================
// Sphere — Sphere support (deferred to analytical pipeline to avoid normalization but kept as is)
// ============================================================================

math::Vec3 supportSphereSphere(const SphereGeom& a, const SphereGeom& b, const math::Vec3& dir) {
    //for any sphere, the farthest point in a direction is simply that direction (scaled by radius)
    //math::Vec3 supportA = a.getPosition() + dir * a.getRadius();
    //math::Vec3 supportB = b.getPosition() - dir * b.getRadius(); //invert direction for minkowski difference support calculation
    //below code is factored version of above
    return a.getPosition() - b.getPosition() + (dir.normalize()) * (a.getRadius() + b.getRadius());
}

// ============================================================================
// Sphere — AABB support
// ============================================================================

math::Vec3 supportSphereAABB(const SphereGeom& a, const AABBGeom& b, const math::Vec3& dir) {
    //for any sphere, the farthest point in a direction is simply that direction (scaled by radius)
    math::Vec3 knowns = a.getPosition() + dir.normalize() * a.getRadius()        - b.getPosition(); //include b.getposition now


    //for box shapes, multiple points can be the same furthest distance away
    //in this case their centroid must be computed
    //but since this is an aabb, this is easy we can use the sign of the direction vector itself
    return knowns + (~dir).hadamard(b.getScales());
    //hadamard (element wise) product takes care of any and all comparisons
    //any cases with multiple same distance points <==> one of the dir axes is 0 <==> contribution canceled out by zpp
    //ex: direction is simply x axis <==> yz = 0 for dir <==> contribution is x only (sign already determined by sign of dir.x) (this is what we want, no yz contribution/elements)
}


// ============================================================================
// Sphere — OBB support
// ============================================================================

math::Vec3 supportSphereOBB(const SphereGeom& a, const OBBGeom& b, const math::Vec3& dir) {
    //for any sphere, the farthest point in a direction is simply that direction (scaled by radius)
    math::Vec3 knowns = a.getPosition() + dir.normalize() * a.getRadius()        - b.getPosition(); //include b.getposition now


    //for box shapes, multiple points can be the same furthest distance away
    //in this case their centroid must be computed
    //for obbs this is a bit more complex but we can do it in obb space to reuse aabb technique
    //thankfully rotation matrices are orthonormal so their inverse is their transpose (makes this computationally light)
    math::Mat3 inv = b.getRotTransform().transpose();
    math::Vec3 local = inv * dir;
    //do aabb computation but in local, obb, space (b.getscales also local)
    local = (~local).hadamard(b.getScales());
    return knowns + b.getRotTransform() * local;
    //hadamard (element wise) product takes care of any and all comparisons
    //any cases with multiple same distance points <==> one of the dir axes is 0 <==> contribution canceled out by zpp
    //ex: direction is simply x axis <==> yz = 0 for dir <==> contribution is x only (sign already determined by sign of dir.x) (this is what we want, no yz contribution/elements)
}

// ============================================================================
// AABB — AABB support
// ============================================================================


math::Vec3 supportAABBAABB(const AABBGeom& a, const AABBGeom& b, const math::Vec3& dir) {
    //using the above defined aabb support technique, this can be a oneliner (with some factoring)
    return a.getPosition() - b.getPosition() + (~dir).hadamard(a.getScales() + b.getScales());
}

// ============================================================================
// AABB — OBB support
// ============================================================================


math::Vec3 supportAABBOBB(const AABBGeom& a, const OBBGeom& b, const math::Vec3& dir) {
    //compute both in wordl space and THEN add
    //use aabb technique
    math::Vec3 knowns = a.getPosition() + (~dir).hadamard(a.getScales())        - b.getPosition(); //include b.getposition now

    //for box shapes, multiple points can be the same furthest distance away
    //in this case their centroid must be computed
    //for obbs this is a bit more complex but we can do it in obb space to reuse aabb technique
    //thankfully rotation matrices are orthonormal so their inverse is their transpose (makes this computationally light)
    math::Mat3 inv = b.getRotTransform().transpose();
    math::Vec3 local = inv * dir;
    //do aabb computation but in local, obb, space (b.getscales also local)
    local = (~local).hadamard(b.getScales());
    return knowns + b.getRotTransform() * local;
    //hadamard (element wise) product takes care of any and all comparisons
    //any cases with multiple same distance points <==> one of the dir axes is 0 <==> contribution canceled out by zpp
    //ex: direction is simply x axis <==> yz = 0 for dir <==> contribution is x only (sign already determined by sign of dir.x) (this is what we want, no yz contribution/elements)
}

// ============================================================================
// OBB — OBB support
// ============================================================================


math::Vec3 supportOBBOBB(const OBBGeom& a, const OBBGeom& b, const math::Vec3& dir) {
    math::Vec3 support = a.getPosition() - b.getPosition();
    //use the obb technique twice (no other way than to just convert to world space for both)
    math::Mat3 invA = a.getRotTransform().transpose();
    math::Vec3 localA = invA * dir;
    //this is the point selection (aabb style but in obb space so ti works)
    localA = (~localA).hadamard(a.getScales());
    //back to world
    support += a.getRotTransform() * localA;

    math::Mat3 invB = b.getRotTransform().transpose();
    math::Vec3 localB = invB * dir;
    //this is the point selection (aabb style but in obb space so ti works)
    localB = (~localB).hadamard(b.getScales());
    //back to world and add both together
    return support + b.getRotTransform() * localB;
}

// ============================================================================
// Capsule + Cylinder per-pair support
// ============================================================================

math::Vec3 supportSphereCapsule(const SphereGeom& a, const CapsuleGeom& b, const math::Vec3& dir) {
    return math::Vec3{}; //no need to implement, deferred to analytical anyway
}
math::Vec3 supportSphereCylinder(const SphereGeom& a, const CylinderGeom& b, const math::Vec3& dir) {
    math::Vec3 support = a.getPosition() - b.getPosition();

    //first transform direction into local space
    math::Vec3 localDir = b.getRotTransform().transpose() * dir;

    //compute the length squared for x and z for x and z normalization for cylinder
    float xzDenom = localDir.x * localDir.x + localDir.z * localDir.z;

    math::Vec3 cylinderContribution = {0.0f,0.0f,0.0f};

    //most common path
    if(xzDenom > 1e-6f) [[likely]]{
        float inv = 1.0f / std::sqrt(xzDenom);
        //now set x and z to their normalized components
        cylinderContribution.x = inv * localDir.x * b.getRadius();
        cylinderContribution.z = inv * localDir.z * b.getRadius();
    }
    //the else is the case of a zero dir but we leave it blank aka no cylinderContribution.x&z remain 0
    //no cylinderContribution in x&z <==> support remains in center which is exactly what we want

    //now set y to the top/bottom according to the sign of the direction (~dir trick but in one dim)
    cylinderContribution.y = std::copysign(b.getHalfHeight(), localDir.y);

    //now use xz denom to compute the inverse for the sphere normalization since direction doesn't matter for sphere
    float total = xzDenom + localDir.y * localDir.y;
    math::Vec3 sphereContribution = (localDir / std::sqrt(total)) * a.getRadius();

    //return total support (after transforming both back to world)
    return support + b.getRotTransform() * (sphereContribution + cylinderContribution);
}
math::Vec3 supportAABBCapsule(const AABBGeom& a, const CapsuleGeom& b, const math::Vec3& dir) {
    //use ~dir hadamard computation for aabb support
    math::Vec3 support = a.getPosition() + (~dir).hadamard(a.getScales()) - b.getPosition();

    //extract the capsule up axis
    const auto& m = b.getRotTransform().m;
    math::Vec3 worldUp = { m[3], m[4], m[5] };

    //now apply ~dir trick but in one dimension on the dotproduct of worldUp with dir
    //if worldUp.dot(dir) > 0 <==> choose the top half and < 0 <==> choose the bottom half
    //so we can literally just copy the sign of the resultant dot onto halfheight to sign it (since its the exact same)
    float signedHeight = std::copysign(b.getHalfHeight(), worldUp.dot(dir));

    //now normalization is regular (same as sphere since capsule = swept sphere) AND we remained in world space
    //so we can do the rest in one line
    return support + (dir.normalize() * b.getRadius()) + worldUp * signedHeight;
}
math::Vec3 supportAABBCylinder(const AABBGeom& a, const CylinderGeom& b, const math::Vec3& dir) {
    //use ~dir hadamard computation for aabb support
    math::Vec3 support = a.getPosition() + (~dir).hadamard(a.getScales()) - b.getPosition();

    //first transform direction into local space
    math::Vec3 localDir = b.getRotTransform().transpose() * dir;

    //compute the length squared for x and z for x and z normalization for cylinder
    float xzDenom = localDir.x * localDir.x + localDir.z * localDir.z;

    math::Vec3 cylinderContribution = {0.0f,0.0f,0.0f};

    //most common path
    if(xzDenom > 1e-6f) [[likely]]{
        float inv = 1.0f / std::sqrt(xzDenom);
        //now set x and z to their normalized components
        cylinderContribution.x = inv * localDir.x * b.getRadius();
        cylinderContribution.z = inv * localDir.z * b.getRadius();
    }
    //the else is the case of a zero dir but we leave it blank aka no cylinderContribution.x&z remain 0
    //no cylinderContribution in x&z <==> support remains in center which is exactly what we want

    //now set y to the top/bottom according to the sign of the direction (~dir trick but in one dim)
    cylinderContribution.y = std::copysign(b.getHalfHeight(), localDir.y);

    //now return total support (after transforming back to world)
    return support + b.getRotTransform() * cylinderContribution;
}
math::Vec3 supportOBBCapsule(const OBBGeom& a, const CapsuleGeom& b, const math::Vec3& dir) {
    math::Vec3 support = a.getPosition() - b.getPosition();

    //use usual obb routine for support calculation
    math::Mat3 invA = a.getRotTransform().transpose();
    math::Vec3 localA = invA * dir;
    //this is the point selection (aabb style but in obb space so ti works)
    localA = (~localA).hadamard(a.getScales());
    //back to world
    support += a.getRotTransform() * localA;

    //extract the capsule up axis
    const auto& m = b.getRotTransform().m;
    math::Vec3 worldUp = { m[3], m[4], m[5] };

    //now apply ~dir trick but in one dimension on the dotproduct of worldUp with dir
    //if worldUp.dot(dir) > 0 <==> choose the top half and < 0 <==> choose the bottom half
    //so we can literally just copy the sign of the resultant dot onto halfheight to sign it (since its the exact same)
    float signedHeight = std::copysign(b.getHalfHeight(), worldUp.dot(dir));

    //now normalization is regular (same as sphere since capsule = swept sphere) AND we remained in world space
    //so we can do the rest in one line
    return support + (dir.normalize() * b.getRadius()) + worldUp * signedHeight;
}
math::Vec3 supportOBBCylinder(const OBBGeom& a, const CylinderGeom& b, const math::Vec3& dir) {
    math::Vec3 support = a.getPosition() - b.getPosition();

    //use usual obb routine for support calculation
    math::Mat3 invA = a.getRotTransform().transpose();
    math::Vec3 localA = invA * dir;
    //this is the point selection (aabb style but in obb space so ti works)
    localA = (~localA).hadamard(a.getScales());
    //back to world
    support += a.getRotTransform() * localA;

    //first transform direction into local space
    math::Vec3 localDir = b.getRotTransform().transpose() * dir;

    //compute the length squared for x and z for x and z normalization for cylinder
    float xzDenom = localDir.x * localDir.x + localDir.z * localDir.z;

    math::Vec3 cylinderContribution = {0.0f,0.0f,0.0f};

    //most common path
    if(xzDenom > 1e-6f) [[likely]]{
        float inv = 1.0f / std::sqrt(xzDenom);
        //now set x and z to their normalized components
        cylinderContribution.x = inv * localDir.x * b.getRadius();
        cylinderContribution.z = inv * localDir.z * b.getRadius();
    }
    //the else is the case of a zero dir but we leave it blank aka no cylinderContribution.x&z remain 0
    //no cylinderContribution in x&z <==> support remains in center which is exactly what we want

    //now set y to the top/bottom according to the sign of the direction (~dir trick but in one dim)
    cylinderContribution.y = std::copysign(b.getHalfHeight(), localDir.y);

    //now return total support (after transforming back to world)
    return support + b.getRotTransform() * cylinderContribution;
}
math::Vec3 supportCapsuleCapsule(const CapsuleGeom& a, const CapsuleGeom& b, const math::Vec3& dir) {
    return math::Vec3{}; //no need to implement, deferred to analytical
}
math::Vec3 supportCapsuleCylinder(const CapsuleGeom& a, const CylinderGeom& b, const math::Vec3& dir) {
    math::Vec3 support = a.getPosition() - b.getPosition();

    //extract the capsule up axis
    const auto& m = a.getRotTransform().m;
    math::Vec3 worldUp = { m[3], m[4], m[5] };

    //now apply ~dir trick but in one dimension on the dotproduct of worldUp with dir
    //if worldUp.dot(dir) > 0 <==> choose the top half and < 0 <==> choose the bottom half
    //so we can literally just copy the sign of the resultant dot onto halfheight to sign it (since its the exact same)
    float signedHeight = std::copysign(b.getHalfHeight(), worldUp.dot(dir));

    //now normalization is regular (same as sphere since capsule = swept sphere) AND we remained in world space
    //so we add this to the running support variable
    support += (dir.normalize() * b.getRadius()) + worldUp * signedHeight;

    //first transform direction into local space
    math::Vec3 localDir = b.getRotTransform().transpose() * dir;

    //compute the length squared for x and z for x and z normalization for cylinder
    float xzDenom = localDir.x * localDir.x + localDir.z * localDir.z;

    math::Vec3 cylinderContribution = {0.0f,0.0f,0.0f};

    //most common path
    if(xzDenom > 1e-6f) [[likely]]{
        float inv = 1.0f / std::sqrt(xzDenom);
        //now set x and z to their normalized components
        cylinderContribution.x = inv * localDir.x * b.getRadius();
        cylinderContribution.z = inv * localDir.z * b.getRadius();
    }
    //the else is the case of a zero dir but we leave it blank aka no cylinderContribution.x&z remain 0
    //no cylinderContribution in x&z <==> support remains in center which is exactly what we want

    //now set y to the top/bottom according to the sign of the direction (~dir trick but in one dim)
    cylinderContribution.y = std::copysign(b.getHalfHeight(), localDir.y);

    //now return total support (after transforming back to world)
    return support + b.getRotTransform() * cylinderContribution;

}
math::Vec3 supportCylinderCylinder(const CylinderGeom& a, const CylinderGeom& b, const math::Vec3& dir) {
    math::Vec3 support = a.getPosition() - b.getPosition();

    //first transform direction into local space
    math::Vec3 localDirA = a.getRotTransform().transpose() * dir;

    //compute the length squared for x and z for x and z normalization for cylinder
    float xzDenomA = localDirA.x * localDirA.x + localDirA.z * localDirA.z;

    math::Vec3 cylinderContributionA = {0.0f,0.0f,0.0f};

    //most common path
    if(xzDenomA > 1e-6f) [[likely]]{
        float inv = 1.0f / std::sqrt(xzDenomA);
        //now set x and z to their normalized components
        cylinderContributionA.x = inv * localDirA.x * a.getRadius();
        cylinderContributionA.z = inv * localDirA.z * a.getRadius();
    }
    //the else is the case of a zero dir but we leave it blank aka no cylinderContribution.x&z remain 0
    //no cylinderContribution in x&z <==> support remains in center which is exactly what we want

    //now set y to the top/bottom according to the sign of the direction (~dir trick but in one dim)
    cylinderContributionA.y = std::copysign(a.getHalfHeight(), localDirA.y);

    //first transform direction into local space
    math::Vec3 localDirB = b.getRotTransform().transpose() * dir;

    //compute the length squared for x and z for x and z normalization for cylinder
    float xzDenomB = localDirB.x * localDirB.x + localDirB.z * localDirB.z;

    math::Vec3 cylinderContributionB = {0.0f,0.0f,0.0f};

    //most common path
    if(xzDenomB > 1e-6f) [[likely]]{
        float inv = 1.0f / std::sqrt(xzDenomB);
        //now set x and z to their normalized components
        cylinderContributionB.x = inv * localDirB.x * b.getRadius();
        cylinderContributionB.z = inv * localDirB.z * b.getRadius();
    }
    //the else is the case of a zero dir but we leave it blank aka no cylinderContribution.x&z remain 0
    //no cylinderContribution in x&z <==> support remains in center which is exactly what we want

    //now set y to the top/bottom according to the sign of the direction (~dir trick but in one dim)
    cylinderContributionB.y = std::copysign(b.getHalfHeight(), localDirB.y);

    //return total support (after transforming both back to world)
    return support + a.getRotTransform() * (cylinderContributionA) + b.getRotTransform() * (cylinderContributionB);
}

// ============================================================================
// Distance-mode witness support — 25 pairwise stubs (see header). Bodies left
// empty; user implements per pair, adapting the corresponding support*() function
// above to also return onA/onB instead of just their difference.
// ============================================================================

SupportWitness supportWithWitness(const SphereGeom&   a, const SphereGeom&   b, const math::Vec3& d) { return {}; }
SupportWitness supportWithWitness(const SphereGeom&   a, const AABBGeom&     b, const math::Vec3& d) { return {}; }
SupportWitness supportWithWitness(const SphereGeom&   a, const OBBGeom&      b, const math::Vec3& d) { return {}; }
SupportWitness supportWithWitness(const AABBGeom&     a, const SphereGeom&   b, const math::Vec3& d) { return {}; }
SupportWitness supportWithWitness(const AABBGeom&     a, const AABBGeom&     b, const math::Vec3& d) { return {}; }
SupportWitness supportWithWitness(const AABBGeom&     a, const OBBGeom&      b, const math::Vec3& d) { return {}; }
SupportWitness supportWithWitness(const OBBGeom&      a, const SphereGeom&   b, const math::Vec3& d) { return {}; }
SupportWitness supportWithWitness(const OBBGeom&      a, const AABBGeom&     b, const math::Vec3& d) { return {}; }
SupportWitness supportWithWitness(const OBBGeom&      a, const OBBGeom&      b, const math::Vec3& d) { return {}; }
// Capsule overloads
SupportWitness supportWithWitness(const SphereGeom&   a, const CapsuleGeom&  b, const math::Vec3& d) { return {}; }
SupportWitness supportWithWitness(const CapsuleGeom&  a, const SphereGeom&   b, const math::Vec3& d) { return {}; }
SupportWitness supportWithWitness(const AABBGeom&     a, const CapsuleGeom&  b, const math::Vec3& d) { return {}; }
SupportWitness supportWithWitness(const CapsuleGeom&  a, const AABBGeom&     b, const math::Vec3& d) { return {}; }
SupportWitness supportWithWitness(const OBBGeom&      a, const CapsuleGeom&  b, const math::Vec3& d) { return {}; }
SupportWitness supportWithWitness(const CapsuleGeom&  a, const OBBGeom&      b, const math::Vec3& d) { return {}; }
SupportWitness supportWithWitness(const CapsuleGeom&  a, const CapsuleGeom&  b, const math::Vec3& d) { return {}; }
// Cylinder overloads
SupportWitness supportWithWitness(const SphereGeom&   a, const CylinderGeom& b, const math::Vec3& d) { return {}; }
SupportWitness supportWithWitness(const CylinderGeom& a, const SphereGeom&   b, const math::Vec3& d) { return {}; }
SupportWitness supportWithWitness(const AABBGeom&     a, const CylinderGeom& b, const math::Vec3& d) { return {}; }
SupportWitness supportWithWitness(const CylinderGeom& a, const AABBGeom&     b, const math::Vec3& d) { return {}; }
SupportWitness supportWithWitness(const OBBGeom&      a, const CylinderGeom& b, const math::Vec3& d) { return {}; }
SupportWitness supportWithWitness(const CylinderGeom& a, const OBBGeom&      b, const math::Vec3& d) { return {}; }
SupportWitness supportWithWitness(const CapsuleGeom&  a, const CylinderGeom& b, const math::Vec3& d) { return {}; }
SupportWitness supportWithWitness(const CylinderGeom& a, const CapsuleGeom&  b, const math::Vec3& d) { return {}; }
SupportWitness supportWithWitness(const CylinderGeom& a, const CylinderGeom& b, const math::Vec3& d) { return {}; }

// Unified overload set — lets std::visit dispatch by type automatically
// Existing 9 (sphere/AABB/OBB) — unchanged
math::Vec3 support(const SphereGeom& a, const SphereGeom& b, const math::Vec3& d) { return  supportSphereSphere(a, b, d); }
math::Vec3 support(const SphereGeom& a, const AABBGeom&   b, const math::Vec3& d) { return  supportSphereAABB(a, b, d);   }
math::Vec3 support(const SphereGeom& a, const OBBGeom&    b, const math::Vec3& d) { return  supportSphereOBB(a, b, d);    }
math::Vec3 support(const AABBGeom&   a, const SphereGeom& b, const math::Vec3& d) { return -supportSphereAABB(b, a, -d);  } // flip
math::Vec3 support(const AABBGeom&   a, const AABBGeom&   b, const math::Vec3& d) { return  supportAABBAABB(a, b, d);     }
math::Vec3 support(const AABBGeom&   a, const OBBGeom&    b, const math::Vec3& d) { return  supportAABBOBB(a, b, d);      }
math::Vec3 support(const OBBGeom&    a, const SphereGeom& b, const math::Vec3& d) { return -supportSphereOBB(b, a, -d);   } // flip
math::Vec3 support(const OBBGeom&    a, const AABBGeom&   b, const math::Vec3& d) { return -supportAABBOBB(b, a, -d);     } // flip
math::Vec3 support(const OBBGeom&    a, const OBBGeom&    b, const math::Vec3& d) { return  supportOBBOBB(a, b, d);       }
// Capsule pairs (16 new ordered combos)
math::Vec3 support(const SphereGeom&   a, const CapsuleGeom&  b, const math::Vec3& d) { return  supportSphereCapsule(a, b, d);   }
math::Vec3 support(const CapsuleGeom&  a, const SphereGeom&   b, const math::Vec3& d) { return -supportSphereCapsule(b, a, -d);  } // flip
math::Vec3 support(const AABBGeom&     a, const CapsuleGeom&  b, const math::Vec3& d) { return  supportAABBCapsule(a, b, d);     }
math::Vec3 support(const CapsuleGeom&  a, const AABBGeom&     b, const math::Vec3& d) { return -supportAABBCapsule(b, a, -d);    } // flip
math::Vec3 support(const OBBGeom&      a, const CapsuleGeom&  b, const math::Vec3& d) { return  supportOBBCapsule(a, b, d);      }
math::Vec3 support(const CapsuleGeom&  a, const OBBGeom&      b, const math::Vec3& d) { return -supportOBBCapsule(b, a, -d);     } // flip
math::Vec3 support(const CapsuleGeom&  a, const CapsuleGeom&  b, const math::Vec3& d) { return  supportCapsuleCapsule(a, b, d);  }
// Cylinder pairs
math::Vec3 support(const SphereGeom&   a, const CylinderGeom& b, const math::Vec3& d) { return  supportSphereCylinder(a, b, d);  }
math::Vec3 support(const CylinderGeom& a, const SphereGeom&   b, const math::Vec3& d) { return -supportSphereCylinder(b, a, -d); } // flip
math::Vec3 support(const AABBGeom&     a, const CylinderGeom& b, const math::Vec3& d) { return  supportAABBCylinder(a, b, d);    }
math::Vec3 support(const CylinderGeom& a, const AABBGeom&     b, const math::Vec3& d) { return -supportAABBCylinder(b, a, -d);   } // flip
math::Vec3 support(const OBBGeom&      a, const CylinderGeom& b, const math::Vec3& d) { return  supportOBBCylinder(a, b, d);     }
math::Vec3 support(const CylinderGeom& a, const OBBGeom&      b, const math::Vec3& d) { return -supportOBBCylinder(b, a, -d);    } // flip
math::Vec3 support(const CapsuleGeom&  a, const CylinderGeom& b, const math::Vec3& d) { return  supportCapsuleCylinder(a, b, d); }
math::Vec3 support(const CylinderGeom& a, const CapsuleGeom&  b, const math::Vec3& d) { return -supportCapsuleCylinder(b, a, -d);} // flip
math::Vec3 support(const CylinderGeom& a, const CylinderGeom& b, const math::Vec3& d) { return  supportCylinderCylinder(a, b, d);}


math::Vec3 shapePosition(const ShapeVariant& v) {
    return std::visit([](const auto& s) { return s.getPosition(); }, v);
}

ShapeVariant makeShapeVariant(ecs::Entity& e, ecs::Registry& reg)  {
    auto* tf = reg.get<Transform>(e);
    if (auto* s = reg.get<ecs::SphereForm>(e))
        return SphereGeom{tf->position, s->radius};
    if (auto* a = reg.get<ecs::AABBForm>(e))
        return AABBGeom{tf->position, a->extent};
    if (auto* c = reg.get<ecs::OBBForm>(e))
        return OBBGeom{tf->position, c->extent, math::Mat3::rotation(tf->rotation)};
    if (auto* c = reg.get<ecs::CapsuleForm>(e))
        return CapsuleGeom{tf->position, c->radius, c->halfHeight, math::Mat3::rotation(tf->rotation)};
    if (auto* c = reg.get<ecs::CylinderForm>(e))
        return CylinderGeom{tf->position, c->radius, c->halfHeight, math::Mat3::rotation(tf->rotation)};
    return SphereGeom{tf->position, 0.0f}; // fallback
}
} // namespace ColliderDiscrete
} // namespace physics
