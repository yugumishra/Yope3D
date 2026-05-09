#include "CAABB.h"

namespace physics {

CAABB::CAABB(math::Vec3 ext, float mass, math::Vec3 pos, math::Vec3 vel)
    : Hull(pos, vel, mass), extent(ext)
{
    initiateState();
}

CAABB::CAABB(math::Vec3 pos, math::Vec3 ext)
    : Hull(pos, {}, 1.0f), extent(ext)
{
    fix();
    initiateState();
}

math::Mat3 CAABB::genInverseInertiaTensor() const {
    if (fixed) return {};
    math::Mat3 res;
    res.m[0] = 3.0f / (mass * (extent.y*extent.y + extent.z*extent.z));
    res.m[4] = 3.0f / (mass * (extent.x*extent.x + extent.z*extent.z));
    res.m[8] = 3.0f / (mass * (extent.x*extent.x + extent.y*extent.y));
    return res;
}

bool CAABB::inside(const math::Vec3& p) const {
    math::Vec3 mn = transform.position - extent;
    math::Vec3 mx = transform.position + extent;
    return p.x >= mn.x && p.x <= mx.x
        && p.y >= mn.y && p.y <= mx.y
        && p.z >= mn.z && p.z <= mx.z;
}

void CAABB::detectCollision(CSphere&) {}
void CAABB::detectCollision(CAABB&)   {}
void CAABB::detectCollision(COBB&)    {}
void CAABB::handleCollision(CSphere&) {}
void CAABB::handleCollision(CAABB&)   {}
void CAABB::handleCollision(COBB&)    {}

} // namespace physics
