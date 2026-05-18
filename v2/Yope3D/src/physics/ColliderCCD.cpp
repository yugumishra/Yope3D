#include "ColliderCCD.h"
#include "CSphere.h"
#include "CAABB.h"
#include "Barrier.h"
#include "BoundedBarrier.h"
#include "PhysicsConstants.h"
#include <cmath>

namespace physics::ColliderCCD {

namespace {

// Returns false only when the swept point is entirely outside [-scale, scale] for the whole step.
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

// CCD sphere vs infinite barrier plane.
static void sphereBarrier(CSphere& one, const Barrier& two, float dt, const math::Vec3& gravity) {
    math::Vec3 diff        = one.getPosition() - two.position;
    math::Vec3 planeNormal = two.normal.normalize();

    // dist > 0 → sphere is already penetrating the barrier surface
    float dist    = one.getRadius() - planeNormal.dot(diff);
    float divider = planeNormal.dot(one.getVelocity() * dt);
    if (std::abs(divider) < EPSILON)
        divider = (divider >= 0.0f) ? 1.0f : -1.0f;
    float t = dist / divider; // fraction of this frame at which surface is reached

    bool willCollide = (t >= 0.0f && t <= 1.0f);
    bool penetrating = (dist > 0.0001f);
    if (!willCollide && !penetrating) return;

    // Partially advance to the contact point when the sphere is moving toward the surface
    // this frame. Skipped when already penetrating (t ≤ 0) to avoid consuming the
    // first-sub-step gravity flag with a zero-distance advance; step 3 integrates the rest.
    if (t > 1e-5f && t <= 1.0f)
        one.advance(t, dt, gravity);

    // Tangency clamp: when at/near the surface and velocity is nearly tangential,
    // zero the inward normal component directly instead of relying on impulse.
    if (dist > -0.1f) {
        float vn  = planeNormal.dot(one.getVelocity());
        float spd = one.getVelocity().length();
        if (vn < 0.0f && spd > 1e-4f && (-vn / spd) < CCD_RESTING_TANGENCY_THRESHOLD)
            one.setVelocity(one.getVelocity() - planeNormal * vn);
    }

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

    float pen = one.getRadius() - planeNormal.dot(one.getPosition() - two.position);
    if (pen > 0)
        one.setPosition(one.getPosition() + planeNormal * (POSITION_CORRECTION * (pen - 0)));
}

// CCD sphere vs bounded barrier (finite wall panel).
//note objects can rest weirdly on bounded barriers since we extend the extent by the relevant radius here
//not going to fix since its really a backport and not necessary
static void sphereBBarrier(CSphere& one, const BoundedBarrier& two, float dt, const math::Vec3& gravity) {
    math::Vec3 principalNormal = two.normal;
    math::Vec3 dir1            = two.orientation;
    math::Vec3 dir2            = two.getSecondOrientation();

    math::Vec3 diff = one.getPosition() - two.position;

    float dist    = one.getRadius() - principalNormal.dot(diff);
    float divider = principalNormal.dot(one.getVelocity() * dt);
    if (std::abs(divider) < EPSILON)
        divider = (divider >= 0.0f) ? 1.0f : -1.0f;
    float t = dist / divider;

    float dir1Bounds = two.xScale + one.getRadius() + CCD_BOUNDED_BARRIER_PADDING;
    float dir2Bounds = two.yScale + one.getRadius() + CCD_BOUNDED_BARRIER_PADDING;

    bool evenPossible = evalDir(dir1, dir1Bounds, diff, one.getVelocity(), dt)
                     && evalDir(dir2, dir2Bounds, diff, one.getVelocity(), dt);
    if (!evenPossible) return;

    bool willCollide = (t >= 0.01f && t <= 1.0f);
    bool penetrating = (dist > 0.0f && dist < CCD_PENETRATION_THRESHOLD);
    if (!willCollide && !penetrating) return;

    if (t > 1e-5f && t <= 1.0f)
        one.advance(t, dt, gravity);

    if (dist > -0.1f) {
        float vn  = principalNormal.dot(one.getVelocity());
        float spd = one.getVelocity().length();
        if (vn < 0.0f && spd > 1e-4f && (-vn / spd) < CCD_RESTING_TANGENCY_THRESHOLD)
            one.setVelocity(one.getVelocity() - principalNormal * vn);
    }

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

    float pen = one.getRadius() - principalNormal.dot(one.getPosition() - two.position);
    if (pen > 0)
        one.setPosition(one.getPosition() + principalNormal * (POSITION_CORRECTION * (pen - 0)));
}

// CCD AABB vs infinite barrier plane.
// The AABB's effective "radius" along the barrier normal is its support:
// rEff = |n.x|*ex + |n.y|*ey + |n.z|*ez.  Same structure as sphereBarrier.
static void aabbBarrier(CAABB& one, const Barrier& two, float dt, const math::Vec3& gravity) {
    math::Vec3 diff = one.getPosition() - two.position;
    math::Vec3 n    = two.normal.normalize();
    math::Vec3 ae   = one.getScales();

    float rEff    = std::abs(n.x) * ae.x + std::abs(n.y) * ae.y + std::abs(n.z) * ae.z;
    float dist    = rEff - n.dot(diff);
    float divider = n.dot(one.getVelocity() * dt);
    if (std::abs(divider) < EPSILON)
        divider = (divider >= 0.0f) ? 1.0f : -1.0f;
    float t = dist / divider;

    bool willCollide = (t >= 0.0f && t <= 1.0f);
    bool penetrating = (dist > 0.0001f);
    if (!willCollide && !penetrating) return;

    if (t > 1e-5f && t <= 1.0f)
        one.advance(t, dt, gravity);

    if (dist > -0.1f) {
        float vn  = n.dot(one.getVelocity());
        float spd = one.getVelocity().length();
        if (vn < 0.0f && spd > 1e-4f && (-vn / spd) < CCD_RESTING_TANGENCY_THRESHOLD)
            one.setVelocity(one.getVelocity() - n * vn);
    }

    float dot = n.dot(one.getVelocity());
    if (dot < 0.0f) {
        float factor = (-dot > CCD_MIN_BOUNCE_VELOCITY) ? CCD_IMPULSE_FACTOR : 1.0f;
        math::Vec3 impulse = n * (dot * -factor * one.getMass());
        one.addImpulse(impulse);
        one.applyImpulses();
    }

    float pen = rEff - n.dot(one.getPosition() - two.position);
    if (pen > 0)
        one.setPosition(one.getPosition() + n * (POSITION_CORRECTION * (pen - 0)));
}

// CCD AABB vs bounded barrier panel.
static void aabbBBarrier(CAABB& one, const BoundedBarrier& two, float dt, const math::Vec3& gravity) {
    math::Vec3 n    = two.normal;
    math::Vec3 dir1 = two.orientation;
    math::Vec3 dir2 = two.getSecondOrientation();
    math::Vec3 diff = one.getPosition() - two.position;
    math::Vec3 ae   = one.getScales();
    math::Vec3 vel  = one.getVelocity();

    float rNorm = std::abs(n.x)*ae.x + std::abs(n.y)*ae.y + std::abs(n.z)*ae.z;
    float rDir1 = std::abs(dir1.x)*ae.x + std::abs(dir1.y)*ae.y + std::abs(dir1.z)*ae.z;
    float rDir2 = std::abs(dir2.x)*ae.x + std::abs(dir2.y)*ae.y + std::abs(dir2.z)*ae.z;

    float dist    = rNorm - n.dot(diff);
    float divider = n.dot(vel * dt);
    if (std::abs(divider) < EPSILON)
        divider = (divider >= 0.0f) ? 1.0f : -1.0f;
    float t = dist / divider;

    float dir1Bounds = two.xScale + rDir1 + CCD_BOUNDED_BARRIER_PADDING;
    float dir2Bounds = two.yScale + rDir2 + CCD_BOUNDED_BARRIER_PADDING;

    bool evenPossible = evalDir(dir1, dir1Bounds, diff, vel, dt)
                     && evalDir(dir2, dir2Bounds, diff, vel, dt);
    if (!evenPossible) return;

    bool willCollide = (t >= 0.01f && t <= 1.0f);
    bool penetrating = (dist > 0.0f && dist < CCD_PENETRATION_THRESHOLD);
    if (!willCollide && !penetrating) return;

    if (t > 1e-5f && t <= 1.0f)
        one.advance(t, dt, gravity);

    if (dist > -0.1f) {
        float vn  = n.dot(one.getVelocity());
        float spd = one.getVelocity().length();
        if (vn < 0.0f && spd > 1e-4f && (-vn / spd) < CCD_RESTING_TANGENCY_THRESHOLD)
            one.setVelocity(one.getVelocity() - n * vn);
    }

    float dot = n.dot(one.getVelocity());
    if (dot < 0.0f) {
        float factor = (-dot > CCD_MIN_BOUNCE_VELOCITY) ? CCD_IMPULSE_FACTOR_BOUNDED : 1.0f;
        math::Vec3 impulse = n * (dot * -factor * one.getMass());
        one.addImpulse(impulse);
        one.applyImpulses();
    }

    float pen = rNorm - n.dot(one.getPosition() - two.position);
    if (pen > 0)
        one.setPosition(one.getPosition() + n * (POSITION_CORRECTION * (pen - 0)));
}

} // anonymous namespace

void collideBarrier(Hull& one, const Barrier& b, float dt, const math::Vec3& gravity) {
    if (auto* s = dynamic_cast<CSphere*>(&one))
        sphereBarrier(*s, b, dt, gravity);
    else if (auto* a = dynamic_cast<CAABB*>(&one))
        aabbBarrier(*a, b, dt, gravity);
}

void collideBarrier(Hull& one, const BoundedBarrier& b, float dt, const math::Vec3& gravity) {
    if (auto* s = dynamic_cast<CSphere*>(&one))
        sphereBBarrier(*s, b, dt, gravity);
    else if (auto* a = dynamic_cast<CAABB*>(&one))
        aabbBBarrier(*a, b, dt, gravity);
}

} // namespace physics::ColliderCCD
