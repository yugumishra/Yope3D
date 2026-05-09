#include "CSphere.h"

namespace physics {

CSphere::CSphere(float mass, float radius, math::Vec3 pos, math::Vec3 vel)
    : Hull(pos, vel, mass), radius(radius)
{
    initiateState();
}

math::Mat3 CSphere::genInverseInertiaTensor() const {
    float i    = 0.4f * mass * radius * radius;
    float invI = (i > 0.0f) ? 1.0f / i : 0.0f;
    math::Mat3 res; // identity baseline
    res.m[0] = invI;
    res.m[4] = invI;
    res.m[8] = invI;
    return res;
}

bool CSphere::inside(const math::Vec3& p) const {
    math::Vec3 d = p - transform.position;
    return d.dot(d) <= radius * radius;
}

// ---- Double-dispatch stubs (filled in when ColliderDiscrete is implemented) ----
void CSphere::detectCollision(CSphere&) {}
void CSphere::detectCollision(CAABB&)   {}
void CSphere::detectCollision(COBB&)    {}
void CSphere::handleCollision(CSphere&) {}
void CSphere::handleCollision(CAABB&)   {}
void CSphere::handleCollision(COBB&)    {}

} // namespace physics
