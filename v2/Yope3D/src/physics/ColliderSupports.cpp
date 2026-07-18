#include "ColliderSupports.h"
#include "../ecs/Registry.h"
#include "../ecs/Components.h"
#include "../world/Transform.h"
#include <cmath>

namespace physics {

namespace ColliderDiscrete {

// ============================================================================
// Single-shape support: furthest point ON that one shape (world space) in the
// given direction. Every pairwise CSO/witness support (see ColliderSupports.h,
// makeSupport/makeSupportWitness) is built generically from these five via
// support(A,B,dir) = supportSingle(A,dir) - supportSingle(B,-dir); no per-pair
// math lives here anymore.
// ============================================================================

math::Vec3 supportSphere(const SphereGeom& s, const math::Vec3& dir) {
    //for any sphere, the farthest point in a direction is simply that direction (scaled by radius)
    return s.getPosition() + (dir.normalize() * s.getRadius());
}

math::Vec3 supportAABB(const AABBGeom& s, const math::Vec3& dir) {
    //for box shapes, multiple points can be the same furthest distance away
    //in this case their centroid must be computed
    //but since this is an aabb, this is easy we can use the sign of the direction vector itself
    return s.getPosition() + (~dir).hadamard(s.getScales());
    //hadamard (element wise) product takes care of any and all comparisons
    //any cases with multiple same distance points <==> one of the dir axes is 0 <==> contribution canceled out by zpp
    //ex: direction is simply x axis <==> yz = 0 for dir <==> contribution is x only (sign already determined by sign of dir.x) (this is what we want, no yz contribution/elements)
}

math::Vec3 supportOBB(const OBBGeom& s, const math::Vec3& dir) {
    //for box shapes, multiple points can be the same furthest distance away
    //in this case their centroid must be computed
    //for obbs this is a bit more complex but we can do it in obb space to reuse aabb technique
    //thankfully rotation matrices are orthonormal so their inverse is their transpose (makes this computationally light)
    math::Mat3 inv = s.getRotTransform().transpose();
    math::Vec3 local = inv * dir;
    //do aabb computation but in local, obb, space (s.getscales also local)
    local = (~local).hadamard(s.getScales());
    return s.getPosition() + s.getRotTransform() * local;
    //hadamard (element wise) product takes care of any and all comparisons
    //any cases with multiple same distance points <==> one of the dir axes is 0 <==> contribution canceled out by zpp
    //ex: direction is simply x axis <==> yz = 0 for dir <==> contribution is x only (sign already determined by sign of dir.x) (this is what we want, no yz contribution/elements)
}

math::Vec3 supportCapsule(const CapsuleGeom& s, const math::Vec3& dir) {
    //extract the capsule up axis
    const auto& m = s.getRotTransform().m;
    math::Vec3 worldUp = { m[3], m[4], m[5] };

    //now apply ~dir trick but in one dimension on the dotproduct of worldUp with dir
    //if worldUp.dot(dir) > 0 <==> choose the top half and < 0 <==> choose the bottom half
    //so we can literally just copy the sign of the resultant dot onto halfheight to sign it (since its the exact same)
    float signedHeight = std::copysign(s.getHalfHeight(), worldUp.dot(dir));

    //now normalization is regular (same as sphere since capsule = swept sphere) AND we remained in world space
    //so we can do the rest in one line
    return s.getPosition() + (dir.normalize() * s.getRadius()) + worldUp * signedHeight;
}

math::Vec3 supportCylinder(const CylinderGeom& s, const math::Vec3& dir) {
    //first transform direction into local space
    math::Vec3 localDir = s.getRotTransform().transpose() * dir;

    //compute the length squared for x and z for x and z normalization for cylinder
    float xzDenom = localDir.x * localDir.x + localDir.z * localDir.z;

    math::Vec3 cylinderContribution = {0.0f,0.0f,0.0f};

    //most common path
    if(xzDenom > 1e-6f) [[likely]]{
        float inv = 1.0f / std::sqrt(xzDenom);
        //now set x and z to their normalized components
        cylinderContribution.x = inv * localDir.x * s.getRadius();
        cylinderContribution.z = inv * localDir.z * s.getRadius();
    }
    //the else is the case of a zero dir but we leave it blank aka no cylinderContribution.x&z remain 0
    //no cylinderContribution in x&z <==> support remains in center which is exactly what we want

    //now set y to the top/bottom according to the sign of the direction (~dir trick but in one dim)
    cylinderContribution.y = std::copysign(s.getHalfHeight(), localDir.y);

    return s.getPosition() + s.getRotTransform() * cylinderContribution;
}

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
