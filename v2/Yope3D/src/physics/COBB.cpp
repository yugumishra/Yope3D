#include "COBB.h"
#include <cmath>

namespace physics {

COBB::COBB(math::Vec3 ext, float mass, math::Vec3 pos, math::Vec3 vel)
    : Hull(pos, vel, mass), extent(ext)
{
    initiateState();
}

std::array<math::Vec3, 3> COBB::getOBBAxes() const {
    const math::Mat3& R = cachedRotTransform;
    return {{
        {R.m[0], R.m[1], R.m[2]},
        {R.m[3], R.m[4], R.m[5]},
        {R.m[6], R.m[7], R.m[8]}
    }};
}

std::array<math::Vec3, 8> COBB::worldSpaceCorners() const {
    auto axes = getOBBAxes();
    math::Vec3 P = transform.position;
    std::array<math::Vec3, 8> corners;
    for (int i = 0; i < 8; ++i) {
        float sx = (i & 1) ? -1.0f : 1.0f;
        float sy = (i & 2) ? -1.0f : 1.0f;
        float sz = (i & 4) ? -1.0f : 1.0f;
        corners[i] = P
            + axes[0] * (sx * extent.x)
            + axes[1] * (sy * extent.y)
            + axes[2] * (sz * extent.z);
    }
    return corners;
}

float COBB::projectOnto(math::Vec3 worldAxis) const {
    auto axes = getOBBAxes();
    return extent.x * std::abs(axes[0].dot(worldAxis))
         + extent.y * std::abs(axes[1].dot(worldAxis))
         + extent.z * std::abs(axes[2].dot(worldAxis));
}

math::Mat3 COBB::genInverseInertiaTensor() const {
    if (fixed) return {};
    math::Mat3 res;
    res.m[0] = 3.0f / (mass * (extent.y*extent.y + extent.z*extent.z));
    res.m[4] = 3.0f / (mass * (extent.x*extent.x + extent.z*extent.z));
    res.m[8] = 3.0f / (mass * (extent.x*extent.x + extent.y*extent.y));
    return res;
}

bool COBB::inside(const math::Vec3& p) const {
    math::Mat3 Rt = cachedRotTransform.transpose();
    math::Vec3 local = Rt * (p - transform.position);
    return std::abs(local.x) <= extent.x
        && std::abs(local.y) <= extent.y
        && std::abs(local.z) <= extent.z;
}

void COBB::detectCollision(CSphere&) {}
void COBB::detectCollision(CAABB&)   {}
void COBB::detectCollision(COBB&)    {}
void COBB::handleCollision(CSphere&) {}
void COBB::handleCollision(CAABB&)   {}
void COBB::handleCollision(COBB&)    {}

} // namespace physics
