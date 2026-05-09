#include "Spring.h"
#include "PhysicsConstants.h"

namespace physics {

Spring::Spring(Hull* a, Hull* b, float k, float rest)
    : first(a), second(b), k(k), restLength(rest) {}

void Spring::update(float dt) {
    math::Vec3 delta = first->getPosition() - second->getPosition();
    float length = delta.length();
    if (length < 1e-7f) return;

    // Spring force: F = k * (length - restLength) * direction
    delta = delta * ((1.0f / length) * (length - restLength) * k);

    // getMass() returns +inf for fixed bodies → velocity change = 0, no special-case needed
    first->addVelocity(delta * (-dt / first->getMass()));
    second->addVelocity(delta * (dt / second->getMass()));

    // Global drag (damps full velocity)
    first->addVelocity(first->getVelocity()   * -SPRING_DAMPING_COEFF);
    second->addVelocity(second->getVelocity() * -SPRING_DAMPING_COEFF);
}

} // namespace physics
