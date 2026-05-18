#include "ColliderDiscrete.h"
#include "CSphere.h"
#include "CAABB.h"
#include "COBB.h"
#include "PhysicsConstants.h"
#include <cmath>
#include <algorithm>

namespace physics::ColliderDiscrete {

// ============================================================================
// One-shot analytical impulse solver — replaces PGS for all pairs.
// Normal convention: from a toward b.
// Angular terms are automatically zero for CAABB (omega=0, applyAngularImpulse no-op).
// ============================================================================

static void directCollisionResponse(Hull& a, Hull& b, const ContactManifold& m) {
    if (m.numContacts == 0) return;

    math::Mat3 IinvA = a.getInverseInertiaTensorWorld();
    math::Mat3 IinvB = b.getInverseInertiaTensorWorld();
    math::Vec3 n     = m.normal;

    for (int i = 0; i < m.numContacts; i++) {
        math::Vec3 rA = m.contactPoints[i] - a.getPosition();
        math::Vec3 rB = m.contactPoints[i] - b.getPosition();

        math::Vec3 relVel = b.getVelocity() + b.getOmega().cross(rB)
                          - a.getVelocity() - a.getOmega().cross(rA);
        float vn = relVel.dot(n);
        if (vn >= 0.0f) continue; // already separating at this contact

        math::Vec3 angA   = (IinvA * rA.cross(n)).cross(rA);
        math::Vec3 angB   = (IinvB * rB.cross(n)).cross(rB);
        float      effMass = a.getInverseMass() + b.getInverseMass()
                           + angA.dot(n) + angB.dot(n);
        if (effMass < 1e-6f) continue;

        float      e       = (-vn > BOUNCE_VELOCITY_THRESHOLD) ? COLLISION_RESTITUTION : 0.0f;
        float      j       = -(1.0f + e) * vn / effMass;
        math::Vec3 impulse = n * j;

        if (!a.isFixed()) { a.addImpulse(-impulse); a.addAngularImpulse(rA.cross(-impulse)); }
        if (!b.isFixed()) { b.addImpulse( impulse); b.addAngularImpulse(rB.cross( impulse)); }
        a.applyImpulses();
        b.applyImpulses();
    }

    // Position correction — push apart by fraction of penetration, weighted by mass
    float pen = std::max(0.0f, m.penetration - POSITION_SLOP);
    if (pen > 0.0f) {
        float totalInv = a.getInverseMass() + b.getInverseMass();
        if (totalInv > 1e-6f) {
            float corr = pen * POSITION_CORRECTION / totalInv;
            if (!a.isFixed()) a.setPosition(a.getPosition() - n * (corr * a.getInverseMass()));
            if (!b.isFixed()) b.setPosition(b.getPosition() + n * (corr * b.getInverseMass()));
        }
    }
}

// ============================================================================
// Sphere — Sphere   (Collider.java::detectSphereSphere / collideSphereSphere)
// ============================================================================

bool detectSphereSphere(const CSphere& a, const CSphere& b, ContactManifold& m) {
    math::Vec3 diff   = b.getPosition() - a.getPosition();
    float      distSq = diff.dot(diff);
    float      rSum   = a.getRadius() + b.getRadius();
    if (distSq > rSum * rSum) return false;

    float dist = std::sqrt(distSq);
    if (dist < 1e-7f) {
        m.normal      = {1.0f, 0.0f, 0.0f};
        m.penetration = rSum;
    } else {
        m.normal      = diff * (1.0f / dist); // from a toward b
        m.penetration = rSum - dist;
    }
    m.numContacts      = 1;
    m.contactPoints[0] = a.getPosition() + m.normal * a.getRadius();
    return true;
}

// ============================================================================
// Sphere — AABB   (Collider.java::detectSphereAABB / collideSphereAABB)
// Normal convention: AABB → sphere (callers flip when sphere is 'a').
// ============================================================================

bool detectSphereAABB(const CSphere& sphere, const CAABB& aabb, ContactManifold& m) {
    math::Vec3 sp = sphere.getPosition();
    math::Vec3 ap = aabb.getPosition();
    math::Vec3 ae = aabb.getScales();
    float      r  = sphere.getRadius();

    math::Vec3 lo = ap - ae;
    math::Vec3 hi = ap + ae;

    math::Vec3 closest = {
        std::max(lo.x, std::min(sp.x, hi.x)),
        std::max(lo.y, std::min(sp.y, hi.y)),
        std::max(lo.z, std::min(sp.z, hi.z))
    };

    math::Vec3 diff   = sp - closest;
    float      distSq = diff.dot(diff);
    if (distSq > r * r) return false;

    m.numContacts      = 1;
    m.contactPoints[0] = closest;

    if (distSq > 1e-8f) {
        float dist    = std::sqrt(distSq);
        m.normal      = diff * (1.0f / dist); // AABB → sphere
        m.penetration = r - dist;
    } else {
        // Sphere centre inside AABB — eject through shallowest face
        float depths[6] = {
            hi.x - sp.x, sp.x - lo.x,
            hi.y - sp.y, sp.y - lo.y,
            hi.z - sp.z, sp.z - lo.z
        };
        math::Vec3 normals[6] = {
            {1,0,0},{-1,0,0},
            {0,1,0},{0,-1,0},
            {0,0,1},{0,0,-1}
        };
        int best = 0;
        for (int i = 1; i < 6; i++)
            if (depths[i] < depths[best]) best = i;

        m.normal      = normals[best];
        m.penetration = r + depths[best];

        math::Vec3 fp = sp;
        switch (best) {
            case 0: fp.x = hi.x; break;  case 1: fp.x = lo.x; break;
            case 2: fp.y = hi.y; break;  case 3: fp.y = lo.y; break;
            case 4: fp.z = hi.z; break;  case 5: fp.z = lo.z; break;
        }
        m.contactPoints[0] = fp;
    }
    return true;
}

// ============================================================================
// AABB — AABB   (Collider.java::collideAABBAABB, y-axis bug fixed)
// Java line 249 compared a.y < a.y (always false). Fixed to a.y < b.y.
// Normal convention: from a toward b (matching directCollisionResponse).
// ============================================================================

bool detectAABBAABB(const CAABB& a, const CAABB& b, ContactManifold& m) {
    math::Vec3 posA = a.getPosition(), posB = b.getPosition();
    math::Vec3 eA   = a.getScales(),   eB   = b.getScales();

    float ovX = (eA.x + eB.x) - std::abs(posA.x - posB.x);
    float ovY = (eA.y + eB.y) - std::abs(posA.y - posB.y);
    float ovZ = (eA.z + eB.z) - std::abs(posA.z - posB.z);

    if (ovX <= 0.0f || ovY <= 0.0f || ovZ <= 0.0f) return false;

    if (ovX <= ovY && ovX <= ovZ) {
        m.normal      = {posA.x < posB.x ? 1.0f : -1.0f, 0.0f, 0.0f};
        m.penetration = ovX;
    } else if (ovY <= ovX && ovY <= ovZ) {
        m.normal      = {0.0f, posA.y < posB.y ? 1.0f : -1.0f, 0.0f};
        m.penetration = ovY;
    } else {
        m.normal      = {0.0f, 0.0f, posA.z < posB.z ? 1.0f : -1.0f};
        m.penetration = ovZ;
    }
    m.numContacts      = 1;
    m.contactPoints[0] = (posA + posB) * 0.5f;
    return true;
}

bool detectSphereOBB(const CSphere&, const COBB&,  ContactManifold&) { return false; }
bool detectAABBOBB  (const CAABB&,   const COBB&,  ContactManifold&) { return false; }
bool detectOBBOBB   (const COBB&,    const COBB&,  ContactManifold&) { return false; }

// ============================================================================
// Top-level dispatch
// Normal must be from a toward b before calling directCollisionResponse.
// detectSphereAABB gives AABB→sphere, so flip when sphere is 'a'.
// ============================================================================

void collide(Hull& a, Hull& b, float /*dt*/) {
    if (a.isFixed() && b.isFixed()) return;

    auto* sa = dynamic_cast<CSphere*>(&a);
    auto* sb = dynamic_cast<CSphere*>(&b);
    auto* aa = dynamic_cast<CAABB*>(&a);
    auto* ab = dynamic_cast<CAABB*>(&b);

    ContactManifold m;

    if (sa && sb) {
        if (detectSphereSphere(*sa, *sb, m))
            directCollisionResponse(a, b, m);
        return;
    }
    if (sa && ab) {
        // detect gives AABB(b)→sphere(a); flip to a→b
        if (detectSphereAABB(*sa, *ab, m)) {
            m.normal = m.normal * -1.0f;
            directCollisionResponse(a, b, m);
        }
        return;
    }
    if (aa && sb) {
        // detect gives AABB(a)→sphere(b); already a→b
        if (detectSphereAABB(*sb, *aa, m))
            directCollisionResponse(a, b, m);
        return;
    }
    if (aa && ab) {
        if (detectAABBAABB(*aa, *ab, m))
            directCollisionResponse(a, b, m);
        return;
    }
}

} // namespace physics::ColliderDiscrete
