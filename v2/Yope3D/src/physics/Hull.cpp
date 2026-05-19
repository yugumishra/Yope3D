#include "Hull.h"
#include "PhysicsConstants.h"
#include <cmath>

namespace physics {

namespace {
    void normalizeQuat(math::Quat& q) {
        float len = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
        if (len > 1e-7f) { q.x /= len; q.y /= len; q.z /= len; q.w /= len; }
    }
}

Hull::Hull(math::Vec3 pos, math::Vec3 vel, float mass, math::Quat rot, math::Vec3 omg)
    : mass(mass), velocity(vel), omega(omg)
{
    transform.position = pos;
    transform.rotation = rot;
}

math::Mat3 Hull::getInverseInertiaTensorWorld() const {
    math::Mat3 R = cachedRotTransform;
    return R * cachedInertiaTensor * R.transpose();
}

void Hull::initiateState() {
    inverseMass         = fixed ? 0.0f : 1.0f / mass;
    cachedInertiaTensor = genInverseInertiaTensor();
    cachedRotTransform  = math::Mat3::rotation(transform.rotation);
    linearImpulse       = {};
    angularImpulse      = {};
    pseudoVel           = {};
    pseudoOmega         = {};
    dtLeft              = 1.0f;
}

void Hull::applyLinearImpulse() {
    if (fixed) { linearImpulse = {}; return; }
    velocity     += linearImpulse * inverseMass;
    linearImpulse = {};
}

void Hull::applyAngularImpulse() {
    if (fixed) { angularImpulse = {}; return; }
    omega         += getInverseInertiaTensorWorld() * angularImpulse;
    angularImpulse = {};
}

void Hull::applyImpulses() {
    applyLinearImpulse();
    applyAngularImpulse();
}

void Hull::advance(float dtPortion, float dt, const math::Vec3& gravity) {
    if (!tangible || fixed) {
        dtLeft -= dtPortion;
        return;
    }

    bool isFirst = (dtLeft >= 1.0f - 1e-6f);
    if (gravity_ && isFirst)
        velocity += gravity * dt;

    float dtActual = dt * dtPortion;
    float linDecay = 1.0f - linearDamping  * dtActual;
    float angDecay = 1.0f - angularDamping * dtActual;
    if (linDecay > 0.0f) velocity = velocity * linDecay;
    if (angDecay > 0.0f) omega    = omega    * angDecay;

    transform.position += velocity * dtActual;

    float omegaLen = omega.length();
    if (omegaLen > 1e-7f) {
        float angle = omegaLen * dtActual;
        math::Quat dq = math::Quat::fromAxisAngle(omega * (1.0f / omegaLen), angle);
        transform.rotation = dq * transform.rotation;
        normalizeQuat(transform.rotation);
    }

    dtLeft -= dtPortion;
}

void Hull::advance(float dt, const math::Vec3& gravity) {
    if (sleeping) { initiateState(); return; }
    if (tangible && !fixed && dtLeft > 1e-6f)
        advance(dtLeft, dt, gravity);
    // Apply pseudo-velocity position correction (split impulse).
    // Pseudo-velocities are integrated with full dt and then discarded.
    if (!fixed && tangible) {
        transform.position += pseudoVel * dt;
        constexpr float MAX_PSEUDO_OMEGA = 2.0f;
        float pOmLen = std::sqrt(pseudoOmega.dot(pseudoOmega));
        if (pOmLen > MAX_PSEUDO_OMEGA) pseudoOmega = pseudoOmega * (MAX_PSEUDO_OMEGA / pOmLen);
        if (pOmLen > 1e-7f) {
            float angle = pOmLen * dt;
            math::Quat dq = math::Quat::fromAxisAngle(pseudoOmega * (1.0f / pOmLen), angle);
            transform.rotation = dq * transform.rotation;
            normalizeQuat(transform.rotation);
        }
    }
    initiateState(); // zeros pseudoVel/pseudoOmega
}

void Hull::tickSleep(float linSpeedSq, float angSpeedSq) {
    if (fixed) return;
    constexpr float linT = SLEEP_LINEAR_THRESHOLD  * SLEEP_LINEAR_THRESHOLD;
    constexpr float angT = SLEEP_ANGULAR_THRESHOLD * SLEEP_ANGULAR_THRESHOLD;
    if (linSpeedSq < linT && angSpeedSq < angT) {
        if (++sleepFrames >= SLEEP_FRAMES_REQUIRED) {
            sleeping  = true;
            velocity  = {};
            omega     = {};
        }
    } else {
        sleepFrames = 0;
        sleeping    = false;
    }
}

} // namespace physics
