#include "ColliderCCD.h"
#include "CSphere.h"
#include "CAABB.h"
#include "Barrier.h"
#include "BoundedBarrier.h"
#include "PhysicsConstants.h"
#include <cmath>

namespace physics::ColliderCCD {

namespace {

// Checks whether the position along `dir` stays within [-scale, scale] during this timestep.
// Returns false only when the point is entirely outside the range throughout [0,1].
static bool evalDir(math::Vec3 dir, float scale, math::Vec3 diff, math::Vec3 v, float dt) {
    float current = diff.dot(dir);
    float step    = (v * dt).dot(dir);

    if (std::abs(step) < 1e-12f)
        return true; // no motion along this axis — treat as in-range

    float t1 = (-scale - current) / step;
    float t2 = ( scale - current) / step;

    if (t1 < 0.0f && t2 < 0.0f) return false; // already past
    if (t1 > 1.0f && t2 > 1.0f) return false; // too far ahead
    return true;
}

// CCD sphere vs infinite barrier plane. Ported from ColliderCCD.java::sphere_barrier.
static void sphereBarrier(CSphere& one, const Barrier& two, float dt) {
    math::Vec3 diff         = one.getPosition() - two.position;
    math::Vec3 planeNormal  = two.normal.normalize();
    math::Vec3 velocityStep = one.getVelocity() * dt;

    // dist > 0  → sphere already penetrating the barrier
    float t    = one.getRadius() - planeNormal.dot(diff);
    float dist = t;

    float divider = planeNormal.dot(velocityStep);
    if (std::abs(divider) < EPSILON)
        divider = (divider >= 0.0f) ? 1.0f : -1.0f;
    t /= divider;

    if (dist > 0.0001f || (t >= 0.0f && t <= 1.0f)) {
        one.setPosition(one.getPosition() + velocityStep * t);

        float sign = (dist > 0.0f) ? 1.0f : (dist < 0.0f ? -1.0f : 0.0f);
        math::Vec3 radiusVec          = planeNormal * (-sign * one.getRadius());
        math::Vec3 tangentialVelocity = radiusVec.cross(one.getOmega());
        math::Vec3 relativeVelocity   = one.getVelocity() + tangentialVelocity;

        float dot = planeNormal.dot(relativeVelocity);
        if (dot < 0.0f) {
            float factor = (-dot > CCD_MIN_BOUNCE_VELOCITY) ? CCD_IMPULSE_FACTOR : 1.0f;
            math::Vec3 impulse = planeNormal * (dot * -factor * one.getMass());
            one.addImpulse(impulse);
            one.applyImpulses();
        }
    }
}

// CCD sphere vs bounded barrier (finite wall panel). Ported from ColliderCCD.java::sphere_bbarrier.
static void sphereBBarrier(CSphere& one, const BoundedBarrier& two, float dt) {
    math::Vec3 principalNormal = two.normal;
    math::Vec3 dir1            = two.orientation;
    math::Vec3 dir2            = two.getSecondOrientation();

    math::Vec3 diff         = one.getPosition() - two.position;
    math::Vec3 velocityStep = one.getVelocity() * dt;

    float t    = one.getRadius() - principalNormal.dot(diff);
    float dist = t;

    float divider = principalNormal.dot(velocityStep);
    if (std::abs(divider) < EPSILON)
        divider = (divider >= 0.0f) ? 1.0f : -1.0f;
    t /= divider;

    float dir1Bounds = two.xScale + one.getRadius() + CCD_BOUNDED_BARRIER_PADDING;
    float dir2Bounds = two.yScale + one.getRadius() + CCD_BOUNDED_BARRIER_PADDING;

    bool evenPossible = evalDir(dir1, dir1Bounds, diff, one.getVelocity(), dt)
                     && evalDir(dir2, dir2Bounds, diff, one.getVelocity(), dt);

    if (evenPossible && ((dist > 0.0f && dist < CCD_PENETRATION_THRESHOLD)
                      || (t >= 0.01f && t <= 1.0f)))
    {
        one.setPosition(one.getPosition() + velocityStep * t);

        float sign = (dist > 0.0f) ? 1.0f : (dist < 0.0f ? -1.0f : 0.0f);
        math::Vec3 radiusVec          = principalNormal * (-sign * one.getRadius());
        math::Vec3 tangentialVelocity = radiusVec.cross(one.getOmega());
        math::Vec3 relativeVelocity   = one.getVelocity() + tangentialVelocity;

        float dot = principalNormal.dot(relativeVelocity);
        if (dot < 0.0f) {
            float factor = (-dot > CCD_MIN_BOUNCE_VELOCITY) ? CCD_IMPULSE_FACTOR_BOUNDED : 1.0f;
            math::Vec3 impulse = principalNormal * (dot * -factor * one.getMass());
            one.addImpulse(impulse);
            one.applyImpulses();
        }
    }
}

// CCD AABB vs infinite barrier plane.
// The AABB's effective "radius" along the barrier normal is its support:
// rEff = |n.x|*ex + |n.y|*ey + |n.z|*ez.  Structurally identical to sphereBarrier.
static void aabbBarrier(CAABB& one, const Barrier& two, float dt) {
    math::Vec3 diff    = one.getPosition() - two.position;
    math::Vec3 n       = two.normal.normalize();
    math::Vec3 velStep = one.getVelocity() * dt;
    math::Vec3 ae      = one.getScales();

    float rEff = std::abs(n.x) * ae.x + std::abs(n.y) * ae.y + std::abs(n.z) * ae.z;

    float dist = rEff - n.dot(diff);
    float t    = dist;

    float divider = n.dot(velStep);
    if (std::abs(divider) < EPSILON)
        divider = (divider >= 0.0f) ? 1.0f : -1.0f;
    t /= divider;

    if (dist > 0.0001f || (t >= 0.0f && t <= 1.0f)) {
        one.setPosition(one.getPosition() + velStep * t);

        float dot = n.dot(one.getVelocity());
        if (dot < 0.0f) {
            float factor = (-dot > CCD_MIN_BOUNCE_VELOCITY) ? CCD_IMPULSE_FACTOR : 1.0f;
            math::Vec3 impulse = n * (dot * -factor * one.getMass());
            one.addImpulse(impulse);
            one.applyImpulses();
        }
    }
}

// CCD AABB vs bounded barrier panel. Tangent-direction bounds are expanded by the
// AABB's support in each tangent direction so evalDir sees the correct swept region.
static void aabbBBarrier(CAABB& one, const BoundedBarrier& two, float dt) {
    math::Vec3 n    = two.normal;
    math::Vec3 dir1 = two.orientation;
    math::Vec3 dir2 = two.getSecondOrientation();
    math::Vec3 diff = one.getPosition() - two.position;
    math::Vec3 ae   = one.getScales();
    math::Vec3 vel  = one.getVelocity();
    math::Vec3 velStep = vel * dt;

    float rNorm = std::abs(n.x)*ae.x + std::abs(n.y)*ae.y + std::abs(n.z)*ae.z;
    float rDir1 = std::abs(dir1.x)*ae.x + std::abs(dir1.y)*ae.y + std::abs(dir1.z)*ae.z;
    float rDir2 = std::abs(dir2.x)*ae.x + std::abs(dir2.y)*ae.y + std::abs(dir2.z)*ae.z;

    float dist = rNorm - n.dot(diff);
    float t    = dist;

    float divider = n.dot(velStep);
    if (std::abs(divider) < EPSILON)
        divider = (divider >= 0.0f) ? 1.0f : -1.0f;
    t /= divider;

    float dir1Bounds = two.xScale + rDir1 + CCD_BOUNDED_BARRIER_PADDING;
    float dir2Bounds = two.yScale + rDir2 + CCD_BOUNDED_BARRIER_PADDING;

    bool evenPossible = evalDir(dir1, dir1Bounds, diff, vel, dt)
                     && evalDir(dir2, dir2Bounds, diff, vel, dt);

    if (evenPossible && ((dist > 0.0f && dist < CCD_PENETRATION_THRESHOLD)
                      || (t >= 0.01f && t <= 1.0f)))
    {
        one.setPosition(one.getPosition() + velStep * t);

        float dot = n.dot(one.getVelocity());
        if (dot < 0.0f) {
            float factor = (-dot > CCD_MIN_BOUNCE_VELOCITY) ? CCD_IMPULSE_FACTOR_BOUNDED : 1.0f;
            math::Vec3 impulse = n * (dot * -factor * one.getMass());
            one.addImpulse(impulse);
            one.applyImpulses();
        }
    }
}

} // anonymous namespace

void collideBarrier(Hull& one, const Barrier& b, float dt) {
    if (auto* s = dynamic_cast<CSphere*>(&one))
        sphereBarrier(*s, b, dt);
    else if (auto* a = dynamic_cast<CAABB*>(&one))
        aabbBarrier(*a, b, dt);
}

void collideBarrier(Hull& one, const BoundedBarrier& b, float dt) {
    if (auto* s = dynamic_cast<CSphere*>(&one))
        sphereBBarrier(*s, b, dt);
    else if (auto* a = dynamic_cast<CAABB*>(&one))
        aabbBBarrier(*a, b, dt);
}

} // namespace physics::ColliderCCD
