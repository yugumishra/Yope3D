#include "ColliderCCD.h"
#include "CSphere.h"
#include "CAABB.h"
#include "COBB.h"
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
    float jn  = 0.0f;
    if (dot < 0.0f) {
        float factor = (-dot > CCD_MIN_BOUNCE_VELOCITY) ? CCD_IMPULSE_FACTOR : 1.0f;
        jn = (-dot) * factor * one.getMass();
        one.addImpulse(planeNormal * jn);
        one.applyImpulses();
    }

    // Coulomb friction (linear only — sphere CCD normal response is also linear-only)
    if (jn > 0.0f && one.friction > 0.0f) {
        math::Vec3 velPost = one.getVelocity();
        // contact arm: bottom of sphere against plane
        math::Vec3 r = planeNormal * (-sign * one.getRadius());
        math::Vec3 cvPost  = velPost + one.getOmega().cross(r);
        math::Vec3 tangVel = cvPost - planeNormal * planeNormal.dot(cvPost);
        float tangSpd = std::sqrt(tangVel.dot(tangVel));
        if (tangSpd > 1e-6f) {
            math::Vec3 tDir = tangVel * (1.0f / tangSpd);
            float vt  = cvPost.dot(tDir);
            float muJ = one.friction * jn;
            float jt  = std::max(-muJ, std::min(muJ, -vt * one.getMass()));
            if (std::abs(jt) > 1e-10f) {
                one.addImpulse(tDir * jt);
                one.applyImpulses();
            }
        }
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
    float jn  = 0.0f;
    if (dot < 0.0f) {
        float factor = (-dot > CCD_MIN_BOUNCE_VELOCITY) ? CCD_IMPULSE_FACTOR_BOUNDED : 1.0f;
        jn = (-dot) * factor * one.getMass();
        one.addImpulse(principalNormal * jn);
        one.applyImpulses();
    }

    // Coulomb friction (linear only)
    if (jn > 0.0f && one.friction > 0.0f) {
        math::Vec3 velPost = one.getVelocity();
        math::Vec3 r = principalNormal * (-sign * one.getRadius());
        math::Vec3 cvPost  = velPost + one.getOmega().cross(r);
        math::Vec3 tangVel = cvPost - principalNormal * principalNormal.dot(cvPost);
        float tangSpd = std::sqrt(tangVel.dot(tangVel));
        if (tangSpd > 1e-6f) {
            math::Vec3 tDir = tangVel * (1.0f / tangSpd);
            float vt  = cvPost.dot(tDir);
            float muJ = one.friction * jn;
            float jt  = std::max(-muJ, std::min(muJ, -vt * one.getMass()));
            if (std::abs(jt) > 1e-10f) {
                one.addImpulse(tDir * jt);
                one.applyImpulses();
            }
        }
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
    float jn  = 0.0f;
    if (dot < 0.0f) {
        float factor = (-dot > CCD_MIN_BOUNCE_VELOCITY) ? CCD_IMPULSE_FACTOR : 1.0f;
        jn = (-dot) * factor * one.getMass();
        one.addImpulse(n * jn);
        one.applyImpulses();
    }

    // Coulomb friction (AABB has no rotation — linear only)
    if (jn > 0.0f && one.friction > 0.0f) {
        math::Vec3 velPost = one.getVelocity();
        math::Vec3 tangVel = velPost - n * n.dot(velPost);
        float tangSpd = std::sqrt(tangVel.dot(tangVel));
        if (tangSpd > 1e-6f) {
            math::Vec3 tDir = tangVel * (1.0f / tangSpd);
            float vt  = velPost.dot(tDir);
            float muJ = one.friction * jn;
            float jt  = std::max(-muJ, std::min(muJ, -vt * one.getMass()));
            if (std::abs(jt) > 1e-10f) {
                one.addImpulse(tDir * jt);
                one.applyImpulses();
            }
        }
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
    float jn  = 0.0f;
    if (dot < 0.0f) {
        float factor = (-dot > CCD_MIN_BOUNCE_VELOCITY) ? CCD_IMPULSE_FACTOR_BOUNDED : 1.0f;
        jn = (-dot) * factor * one.getMass();
        one.addImpulse(n * jn);
        one.applyImpulses();
    }

    // Coulomb friction (AABB has no rotation — linear only)
    if (jn > 0.0f && one.friction > 0.0f) {
        math::Vec3 velPost = one.getVelocity();
        math::Vec3 tangVel = velPost - n * n.dot(velPost);
        float tangSpd = std::sqrt(tangVel.dot(tangVel));
        if (tangSpd > 1e-6f) {
            math::Vec3 tDir = tangVel * (1.0f / tangSpd);
            float vt  = velPost.dot(tDir);
            float muJ = one.friction * jn;
            float jt  = std::max(-muJ, std::min(muJ, -vt * one.getMass()));
            if (std::abs(jt) > 1e-10f) {
                one.addImpulse(tDir * jt);
                one.applyImpulses();
            }
        }
    }

    float pen = rNorm - n.dot(one.getPosition() - two.position);
    if (pen > 0)
        one.setPosition(one.getPosition() + n * (POSITION_CORRECTION * (pen - 0)));
}

// CCD OBB vs infinite barrier.
// Uses projectOnto(normal) as the effective radius — same structure as aabbBarrier.
// Conservative (treats OBB as its AABB envelope along the barrier normal) but prevents tunneling.
static void obbBarrier(COBB& one, const Barrier& two, float dt, const math::Vec3& gravity) {
    math::Vec3 diff = one.getPosition() - two.position;
    math::Vec3 n    = two.normal.normalize();

    float rEff    = one.projectOnto(n);
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

    // Build contact manifold: centroid of all corners penetrating the barrier plane.
    // For a flat face resting on the floor all 4 bottom corners contribute, giving a
    // centroid at the face center and zero net torque — correct for symmetric contact.
    // Degrades to edge (2 corners) or vertex (1 corner) contact automatically.
    auto corners = one.worldSpaceCorners();
    float      planeD = n.dot(two.position);
    math::Vec3 contactPt = {};
    int        numPts = 0;
    for (int i = 0; i < 8; ++i) {
        if (planeD - n.dot(corners[i]) > 0.0f) { contactPt += corners[i]; ++numPts; }
    }
    if (numPts > 0) {
        contactPt = contactPt * (1.0f / numPts);
    } else {
        float minProj = n.dot(corners[0]); contactPt = corners[0];
        for (int i = 1; i < 8; ++i) {
            float p = n.dot(corners[i]);
            if (p < minProj) { minProj = p; contactPt = corners[i]; }
        }
    }
    math::Vec3 r    = contactPt - one.getPosition();
    math::Mat3 Iinv = one.getInverseInertiaTensorWorld();

    math::Vec3 contactVel = one.getVelocity() + one.getOmega().cross(r);
    float vn = n.dot(contactVel);
    float j  = 0.0f;
    if (vn < 0.0f) {
        math::Vec3 rCrossN = r.cross(n);
        float angTerm      = (Iinv * rCrossN).cross(r).dot(n);
        float effMass      = one.getInverseMass() + angTerm;
        if (effMass < 1e-6f) effMass = 1e-6f;

        float e   = (-vn > CCD_MIN_BOUNCE_VELOCITY) ? (CCD_IMPULSE_FACTOR - 1.0f) : 0.0f;
        j         = -(1.0f + e) * vn / effMass;
        math::Vec3 impulse = n * j;
        one.addImpulse(impulse);
        one.addAngularImpulse(r.cross(impulse));
        one.applyImpulses();
    }

    // Coulomb friction along barrier surface
    if (j > 0.0f && one.friction > 0.0f) {
        math::Vec3 cvPost  = one.getVelocity() + one.getOmega().cross(r);
        math::Vec3 tangVel = cvPost - n * n.dot(cvPost);
        float tangSpd = std::sqrt(tangVel.dot(tangVel));
        if (tangSpd > 1e-6f) {
            math::Vec3 tDir    = tangVel * (1.0f / tangSpd);
            math::Vec3 rCrossT = r.cross(tDir);
            float angTermT     = (Iinv * rCrossT).cross(r).dot(tDir);
            float effMassT     = one.getInverseMass() + angTermT;
            if (effMassT > 1e-6f) {
                float vt  = cvPost.dot(tDir);
                float muJ = one.friction * j;
                float jt  = std::max(-muJ, std::min(muJ, -vt / effMassT));
                if (std::abs(jt) > 1e-10f) {
                    math::Vec3 fImp = tDir * jt;
                    one.addImpulse(fImp);
                    one.addAngularImpulse(r.cross(fImp));
                    one.applyImpulses();
                }
            }
        }
    }

    float pen = one.projectOnto(n) - n.dot(one.getPosition() - two.position);
    if (pen > 0)
        one.setPosition(one.getPosition() + n * (POSITION_CORRECTION * pen));
}

// CCD OBB vs bounded barrier — uses OBB's projected extent on each panel axis.
static void obbBBarrier(COBB& one, const BoundedBarrier& two, float dt, const math::Vec3& gravity) {
    math::Vec3 n    = two.normal;
    math::Vec3 dir1 = two.orientation;
    math::Vec3 dir2 = two.getSecondOrientation();
    math::Vec3 diff = one.getPosition() - two.position;
    math::Vec3 vel  = one.getVelocity();

    float rNorm = one.projectOnto(n);
    float rDir1 = one.projectOnto(dir1);
    float rDir2 = one.projectOnto(dir2);

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

    auto corners2 = one.worldSpaceCorners();
    float      planeD2 = n.dot(two.position);
    math::Vec3 contactPt2 = {};
    int        numPts2 = 0;
    for (int i = 0; i < 8; ++i) {
        if (planeD2 - n.dot(corners2[i]) > 0.0f) { contactPt2 += corners2[i]; ++numPts2; }
    }
    if (numPts2 > 0) {
        contactPt2 = contactPt2 * (1.0f / numPts2);
    } else {
        float minProj2 = n.dot(corners2[0]); contactPt2 = corners2[0];
        for (int i = 1; i < 8; ++i) {
            float p = n.dot(corners2[i]);
            if (p < minProj2) { minProj2 = p; contactPt2 = corners2[i]; }
        }
    }
    math::Vec3 r2    = contactPt2 - one.getPosition();
    math::Mat3 Iinv2 = one.getInverseInertiaTensorWorld();

    math::Vec3 contactVel2 = one.getVelocity() + one.getOmega().cross(r2);
    float vn2 = n.dot(contactVel2);
    float j2  = 0.0f;
    if (vn2 < 0.0f) {
        math::Vec3 rCrossN2 = r2.cross(n);
        float angTerm2      = (Iinv2 * rCrossN2).cross(r2).dot(n);
        float effMass2      = one.getInverseMass() + angTerm2;
        if (effMass2 < 1e-6f) effMass2 = 1e-6f;

        float e2 = (-vn2 > CCD_MIN_BOUNCE_VELOCITY) ? (CCD_IMPULSE_FACTOR - 1.0f) : 0.0f;
        j2       = -(1.0f + e2) * vn2 / effMass2;
        math::Vec3 impulse2 = n * j2;
        one.addImpulse(impulse2);
        one.addAngularImpulse(r2.cross(impulse2));
        one.applyImpulses();
    }

    // Coulomb friction
    if (j2 > 0.0f && one.friction > 0.0f) {
        math::Vec3 cvPost2  = one.getVelocity() + one.getOmega().cross(r2);
        math::Vec3 tangVel2 = cvPost2 - n * n.dot(cvPost2);
        float tangSpd2 = std::sqrt(tangVel2.dot(tangVel2));
        if (tangSpd2 > 1e-6f) {
            math::Vec3 tDir2    = tangVel2 * (1.0f / tangSpd2);
            math::Vec3 rCrossT2 = r2.cross(tDir2);
            float angTermT2     = (Iinv2 * rCrossT2).cross(r2).dot(tDir2);
            float effMassT2     = one.getInverseMass() + angTermT2;
            if (effMassT2 > 1e-6f) {
                float vt2  = cvPost2.dot(tDir2);
                float muJ2 = one.friction * j2;
                float jt2  = std::max(-muJ2, std::min(muJ2, -vt2 / effMassT2));
                if (std::abs(jt2) > 1e-10f) {
                    math::Vec3 fImp2 = tDir2 * jt2;
                    one.addImpulse(fImp2);
                    one.addAngularImpulse(r2.cross(fImp2));
                    one.applyImpulses();
                }
            }
        }
    }

    float pen = one.projectOnto(n) - n.dot(one.getPosition() - two.position);
    if (pen > 0)
        one.setPosition(one.getPosition() + n * (POSITION_CORRECTION * pen));
}

} // anonymous namespace

void collideBarrier(Hull& one, const Barrier& b, float dt, const math::Vec3& gravity) {
    if (auto* s = dynamic_cast<CSphere*>(&one))
        sphereBarrier(*s, b, dt, gravity);
    else if (auto* a = dynamic_cast<CAABB*>(&one))
        aabbBarrier(*a, b, dt, gravity);
    else if (auto* o = dynamic_cast<COBB*>(&one))
        obbBarrier(*o, b, dt, gravity);
}

void collideBarrier(Hull& one, const BoundedBarrier& b, float dt, const math::Vec3& gravity) {
    if (auto* s = dynamic_cast<CSphere*>(&one))
        sphereBBarrier(*s, b, dt, gravity);
    else if (auto* a = dynamic_cast<CAABB*>(&one))
        aabbBBarrier(*a, b, dt, gravity);
    else if (auto* o = dynamic_cast<COBB*>(&one))
        obbBBarrier(*o, b, dt, gravity);
}

} // namespace physics::ColliderCCD
