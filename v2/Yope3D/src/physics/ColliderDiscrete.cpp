#include "ColliderDiscrete.h"
#include "CSphere.h"
#include "CAABB.h"
#include "COBB.h"
#include "PhysicsConstants.h"
#include <cmath>
#include <algorithm>

namespace physics::ColliderDiscrete {

// ============================================================================
// PGS (Projected Gauss-Seidel) constraint solver.
// Normal convention: from a toward b.
// Angular terms are automatically zero for CAABB (getOmega()=0, applyAngularImpulse no-op).
// Baumgarte stabilization handles penetration correction — no separate position nudge needed.
// ============================================================================

static void pgsCollisionResponse(Hull& a, Hull& b, const ContactManifold& m, float dt) {
    if (m.numContacts == 0) return;

    math::Mat3 IinvA = a.getInverseInertiaTensorWorld();
    math::Mat3 IinvB = b.getInverseInertiaTensorWorld();
    math::Vec3 n     = m.normal;
    int numIter = (m.numContacts == 1) ? PGS_ITERATIONS_SINGLE : PGS_ITERATIONS_MULTI;

    // Per-contact: precompute effective mass W[i] and Baumgarte+restitution bias neta[i].
    float W[4]      = {};
    float neta[4]   = {};
    float lambda[4] = {}; // accumulated impulse magnitude per contact (clamped >= 0)

    for (int i = 0; i < m.numContacts; i++) {
        math::Vec3 rA = m.contactPoints[i] - a.getPosition();
        math::Vec3 rB = m.contactPoints[i] - b.getPosition();

        math::Vec3 angA    = (IinvA * rA.cross(n)).cross(rA);
        math::Vec3 angB    = (IinvB * rB.cross(n)).cross(rB);
        float      effMass = a.getInverseMass() + b.getInverseMass()
                           + angA.dot(n) + angB.dot(n);
        if (effMass < 1e-6f) continue;
        W[i] = 1.0f / effMass;

        // Baumgarte bias: pushes penetrating bodies apart over several frames.
        float bias = (PGS_BAUMGARTE_FACTOR / dt)
                   * std::max(0.0f, m.penetration - PGS_PENETRATION_SLOP);

        // Restitution: adds outward velocity for bouncy contacts.
        math::Vec3 relVel0 = b.getVelocity() + b.getOmega().cross(rB)
                           - a.getVelocity() - a.getOmega().cross(rA);
        float vn0 = relVel0.dot(n);
        float restitution = (vn0 < -PGS_RESTITUTION_THRESHOLD)
                          ? -PGS_RESTITUTION * vn0   // positive, adds to bias
                          : 0.0f;

        neta[i] = bias + restitution;
    }

    // Gauss-Seidel iteration: apply each contact impulse immediately so later
    // contacts in the same sweep see the updated velocities.
    for (int iter = 0; iter < numIter; iter++) {
        for (int i = 0; i < m.numContacts; i++) {
            if (W[i] == 0.0f) continue;

            math::Vec3 rA = m.contactPoints[i] - a.getPosition();
            math::Vec3 rB = m.contactPoints[i] - b.getPosition();

            // Correct relative velocity at contact point (fixed vs Java: proper sign on angular terms).
            math::Vec3 relVel = b.getVelocity() + b.getOmega().cross(rB)
                              - a.getVelocity() - a.getOmega().cross(rA);
            float vn = relVel.dot(n);

            // Impulse magnitude delta (no 0.5f factor — Java bug removed).
            float deltaLambda = -W[i] * (vn - neta[i]);

            // Project: non-penetration constraint — impulse can only push apart, never pull.
            float oldLambda = lambda[i];
            lambda[i]       = std::max(0.0f, lambda[i] + deltaLambda);
            float dL        = lambda[i] - oldLambda;
            if (std::abs(dL) < 1e-10f) continue;

            math::Vec3 impulse = n * dL;

            // Fixed Java angular sign bug: a gets -impulse, so angular = rA × (-impulse).
            //                              b gets +impulse, so angular = rB × (+impulse).
            if (!a.isFixed()) { a.addImpulse(-impulse); a.addAngularImpulse(rA.cross(-impulse)); }
            if (!b.isFixed()) { b.addImpulse( impulse); b.addAngularImpulse(rB.cross( impulse)); }
            a.applyImpulses();
            b.applyImpulses();
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

// ============================================================================
// Sphere — OBB
// Normal convention: OBB → sphere (from b toward a when sphere=a). Dispatcher flips.
// ============================================================================

bool detectSphereOBB(const CSphere& sphere, const COBB& obb, ContactManifold& m) {
    math::Vec3 sp  = sphere.getPosition();
    math::Vec3 op  = obb.getPosition();
    math::Vec3 ext = obb.getScales();
    float      r   = sphere.getRadius();

    math::Mat3 Rt    = obb.getRotTransform().transpose();
    math::Vec3 local = Rt * (sp - op); // sphere center in OBB local space

    math::Vec3 clamped = {
        std::max(-ext.x, std::min(local.x, ext.x)),
        std::max(-ext.y, std::min(local.y, ext.y)),
        std::max(-ext.z, std::min(local.z, ext.z))
    };

    math::Vec3 worldClosest = op + obb.getRotTransform() * clamped;
    math::Vec3 diff         = sp - worldClosest; // OBB → sphere
    float      distSq       = diff.dot(diff);
    if (distSq >= r * r) return false;

    m.numContacts = 1;
    if (distSq > 1e-8f) {
        float dist        = std::sqrt(distSq);
        m.normal          = diff * (1.0f / dist); // OBB → sphere
        m.penetration     = r - dist;
        m.contactPoints[0] = worldClosest;
    } else {
        // Sphere centre inside OBB — eject through shallowest local face
        float depths[6] = {
            ext.x + local.x, ext.x - local.x,
            ext.y + local.y, ext.y - local.y,
            ext.z + local.z, ext.z - local.z
        };
        math::Vec3 localNormals[6] = {
            {-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}
        };
        int best = 0;
        for (int i = 1; i < 6; i++)
            if (depths[i] < depths[best]) best = i;

        m.normal          = obb.getRotTransform() * localNormals[best]; // world-space outward
        m.penetration     = r + depths[best];
        m.contactPoints[0] = sp - m.normal * r;
    }
    return true;
}

// ============================================================================
// AABB — OBB   15-axis SAT
// Normal convention: from AABB (a) toward OBB (b).
// ============================================================================

bool detectAABBOBB(const CAABB& a, const COBB& b, ContactManifold& m) {
    math::Vec3 aPos = a.getPosition(), aExt = a.getScales();
    math::Vec3 bPos = b.getPosition(), bExt = b.getScales();
    auto       bAxes = b.getOBBAxes();
    math::Vec3 diff  = bPos - aPos;

    const math::Vec3 worldAxes[3] = {{1,0,0},{0,1,0},{0,0,1}};
    float            extArr[3]    = {aExt.x, aExt.y, aExt.z};

    float      minOverlap = 1e30f;
    math::Vec3 bestAxis;
    int        axisType = 0; // 0=AABB face, 1=OBB face, 2=edge
    int        axisIdx  = 0;

    auto testAxis = [&](math::Vec3 n, int type, int idx) -> bool {
        float lenSq = n.dot(n);
        if (lenSq < 1e-8f) return true; // degenerate cross product — skip
        n = n * (1.0f / std::sqrt(lenSq));

        float projA      = std::abs(n.x)*aExt.x + std::abs(n.y)*aExt.y + std::abs(n.z)*aExt.z;
        float projB      = b.projectOnto(n);
        float centerDist = std::abs(diff.dot(n));
        float overlap    = projA + projB - centerDist;
        if (overlap < 0.0f) return false;
        if (overlap < minOverlap) {
            minOverlap = overlap;
            bestAxis   = n;
            axisType   = type;
            axisIdx    = idx;
        }
        return true;
    };

    for (int i = 0; i < 3; i++) if (!testAxis(worldAxes[i], 0, i)) return false;
    for (int i = 0; i < 3; i++) if (!testAxis(bAxes[i],     1, i)) return false;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            if (!testAxis(worldAxes[i].cross(bAxes[j]), 2, i*3+j)) return false;

    // Ensure normal points from AABB (a) toward OBB (b)
    math::Vec3 normal = (diff.dot(bestAxis) >= 0.0f) ? bestAxis : -bestAxis;
    m.normal      = normal;
    m.penetration = minOverlap;

    struct Cand { math::Vec3 pt; float depth; };

    if (axisType == 0) {
        // AABB face axis — test OBB corners against reference AABB face
        // faceD: signed distance of the AABB face plane along normal
        float faceD   = aPos.dot(normal) + extArr[axisIdx];
        int   sa0     = (axisIdx + 1) % 3;
        int   sa1     = (axisIdx + 2) % 3;
        auto  corners = b.worldSpaceCorners();

        Cand cands[8]; int numCands = 0;
        for (auto& c : corners) {
            float depth = faceD - c.dot(normal);
            if (depth <= 0.0f) continue;
            // Check within AABB side bounds
            float d0 = std::abs(c.dot(worldAxes[sa0]) - aPos.dot(worldAxes[sa0]));
            float d1 = std::abs(c.dot(worldAxes[sa1]) - aPos.dot(worldAxes[sa1]));
            if (d0 > extArr[sa0] || d1 > extArr[sa1]) continue;
            cands[numCands++] = {c, depth};
        }
        if (numCands == 0) {
            m.contactPoints[0] = (aPos + bPos) * 0.5f;
            m.numContacts = 1;
        } else {
            for (int i = 1; i < numCands; i++) { // insertion sort descending
                Cand key = cands[i]; int j = i - 1;
                while (j >= 0 && cands[j].depth < key.depth) { cands[j+1] = cands[j]; j--; }
                cands[j+1] = key;
            }
            int k = std::min(numCands, 4);
            for (int i = 0; i < k; i++) m.contactPoints[i] = cands[i].pt;
            m.numContacts = k;
        }
    } else if (axisType == 1) {
        // OBB face axis — test AABB corners against OBB interior
        math::Mat3 Rt       = b.getRotTransform().transpose();
        float      bExtArr[3] = {bExt.x, bExt.y, bExt.z};

        Cand cands[8]; int numCands = 0;
        for (int i = 0; i < 8; i++) {
            float sx = (i & 1) ? -1.0f : 1.0f;
            float sy = (i & 2) ? -1.0f : 1.0f;
            float sz = (i & 4) ? -1.0f : 1.0f;
            math::Vec3 corner = aPos + math::Vec3{sx*aExt.x, sy*aExt.y, sz*aExt.z};
            if (!b.inside(corner)) continue;
            math::Vec3 local    = Rt * (corner - bPos);
            float      localArr[3] = {local.x, local.y, local.z};
            float      depth    = bExtArr[axisIdx] - std::abs(localArr[axisIdx]);
            if (depth <= 0.0f) continue;
            cands[numCands++] = {corner, depth};
        }
        if (numCands == 0) {
            m.contactPoints[0] = (aPos + bPos) * 0.5f;
            m.numContacts = 1;
        } else {
            for (int i = 1; i < numCands; i++) {
                Cand key = cands[i]; int j = i - 1;
                while (j >= 0 && cands[j].depth < key.depth) { cands[j+1] = cands[j]; j--; }
                cands[j+1] = key;
            }
            int k = std::min(numCands, 4);
            for (int i = 0; i < k; i++) m.contactPoints[i] = cands[i].pt;
            m.numContacts = k;
        }
    } else {
        // Edge-edge — single contact at midpoint of deepest support vertices
        math::Vec3 supportA = {
            aPos.x + (normal.x >= 0.0f ? aExt.x : -aExt.x),
            aPos.y + (normal.y >= 0.0f ? aExt.y : -aExt.y),
            aPos.z + (normal.z >= 0.0f ? aExt.z : -aExt.z)
        };
        math::Vec3 supportB = bPos;
        supportB = supportB + bAxes[0] * (normal.dot(bAxes[0]) >= 0.0f ? -bExt.x :  bExt.x);
        supportB = supportB + bAxes[1] * (normal.dot(bAxes[1]) >= 0.0f ? -bExt.y :  bExt.y);
        supportB = supportB + bAxes[2] * (normal.dot(bAxes[2]) >= 0.0f ? -bExt.z :  bExt.z);
        m.contactPoints[0] = (supportA + supportB) * 0.5f;
        m.numContacts = 1;
    }
    return true;
}

// ============================================================================
// OBB — OBB   15-axis SAT
// Normal convention: from OBB a toward OBB b.
// ============================================================================

bool detectOBBOBB(const COBB& a, const COBB& b, ContactManifold& m) {
    math::Vec3 aPos = a.getPosition(), aExt = a.getScales();
    math::Vec3 bPos = b.getPosition(), bExt = b.getScales();
    auto       aAxes = a.getOBBAxes();
    auto       bAxes = b.getOBBAxes();
    math::Vec3 diff  = bPos - aPos;

    float      minOverlap = 1e30f;
    math::Vec3 bestAxis;
    int        axisType = 0; // 0=A face, 1=B face, 2=edge
    int        axisIdx  = 0;

    auto testAxis = [&](math::Vec3 n, int type, int idx) -> bool {
        float lenSq = n.dot(n);
        if (lenSq < 1e-8f) return true;
        n = n * (1.0f / std::sqrt(lenSq));
        float projA      = a.projectOnto(n);
        float projB      = b.projectOnto(n);
        float centerDist = std::abs(diff.dot(n));
        float overlap    = projA + projB - centerDist;
        if (overlap < 0.0f) return false;
        if (overlap < minOverlap) {
            minOverlap = overlap;
            bestAxis   = n;
            axisType   = type;
            axisIdx    = idx;
        }
        return true;
    };

    for (int i = 0; i < 3; i++) if (!testAxis(aAxes[i], 0, i)) return false;
    for (int i = 0; i < 3; i++) if (!testAxis(bAxes[i], 1, i)) return false;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            if (!testAxis(aAxes[i].cross(bAxes[j]), 2, i*3+j)) return false;

    math::Vec3 normal = (diff.dot(bestAxis) >= 0.0f) ? bestAxis : -bestAxis;
    m.normal      = normal;
    m.penetration = minOverlap;

    struct Cand { math::Vec3 pt; float depth; };

    if (axisType == 0) {
        // A face axis — test B corners against A's interior
        math::Mat3 RtA    = a.getRotTransform().transpose();
        float      aEArr[3] = {aExt.x, aExt.y, aExt.z};
        auto       bCorners = b.worldSpaceCorners();

        Cand cands[8]; int numCands = 0;
        for (auto& c : bCorners) {
            if (!a.inside(c)) continue;
            math::Vec3 local    = RtA * (c - aPos);
            float      lArr[3]  = {local.x, local.y, local.z};
            float      depth    = aEArr[axisIdx] - std::abs(lArr[axisIdx]);
            if (depth <= 0.0f) continue;
            cands[numCands++] = {c, depth};
        }
        if (numCands == 0) {
            m.contactPoints[0] = (aPos + bPos) * 0.5f;
            m.numContacts = 1;
        } else {
            for (int i = 1; i < numCands; i++) {
                Cand key = cands[i]; int j = i - 1;
                while (j >= 0 && cands[j].depth < key.depth) { cands[j+1] = cands[j]; j--; }
                cands[j+1] = key;
            }
            int k = std::min(numCands, 4);
            for (int i = 0; i < k; i++) m.contactPoints[i] = cands[i].pt;
            m.numContacts = k;
        }
    } else if (axisType == 1) {
        // B face axis — test A corners against B's interior
        math::Mat3 RtB    = b.getRotTransform().transpose();
        float      bEArr[3] = {bExt.x, bExt.y, bExt.z};
        auto       aCorners = a.worldSpaceCorners();

        Cand cands[8]; int numCands = 0;
        for (auto& c : aCorners) {
            if (!b.inside(c)) continue;
            math::Vec3 local    = RtB * (c - bPos);
            float      lArr[3]  = {local.x, local.y, local.z};
            float      depth    = bEArr[axisIdx] - std::abs(lArr[axisIdx]);
            if (depth <= 0.0f) continue;
            cands[numCands++] = {c, depth};
        }
        if (numCands == 0) {
            m.contactPoints[0] = (aPos + bPos) * 0.5f;
            m.numContacts = 1;
        } else {
            for (int i = 1; i < numCands; i++) {
                Cand key = cands[i]; int j = i - 1;
                while (j >= 0 && cands[j].depth < key.depth) { cands[j+1] = cands[j]; j--; }
                cands[j+1] = key;
            }
            int k = std::min(numCands, 4);
            for (int i = 0; i < k; i++) m.contactPoints[i] = cands[i].pt;
            m.numContacts = k;
        }
    } else {
        // Edge-edge — midpoint of support vertices
        math::Vec3 supportA = aPos;
        supportA = supportA + aAxes[0] * (normal.dot(aAxes[0]) >= 0.0f ?  aExt.x : -aExt.x);
        supportA = supportA + aAxes[1] * (normal.dot(aAxes[1]) >= 0.0f ?  aExt.y : -aExt.y);
        supportA = supportA + aAxes[2] * (normal.dot(aAxes[2]) >= 0.0f ?  aExt.z : -aExt.z);
        math::Vec3 supportB = bPos;
        supportB = supportB + bAxes[0] * (normal.dot(bAxes[0]) >= 0.0f ? -bExt.x :  bExt.x);
        supportB = supportB + bAxes[1] * (normal.dot(bAxes[1]) >= 0.0f ? -bExt.y :  bExt.y);
        supportB = supportB + bAxes[2] * (normal.dot(bAxes[2]) >= 0.0f ? -bExt.z :  bExt.z);
        m.contactPoints[0] = (supportA + supportB) * 0.5f;
        m.numContacts = 1;
    }
    return true;
}

// ============================================================================
// Top-level dispatch
// Normal must be from a toward b before calling pgsCollisionResponse.
// detectSphereAABB gives AABB→sphere, so flip when sphere is 'a'.
// ============================================================================

void collide(Hull& a, Hull& b, float dt) {
    if (a.isFixed() && b.isFixed()) return;

    auto* sa = dynamic_cast<CSphere*>(&a);
    auto* sb = dynamic_cast<CSphere*>(&b);
    auto* aa = dynamic_cast<CAABB*>(&a);
    auto* ab = dynamic_cast<CAABB*>(&b);
    auto* ca = dynamic_cast<COBB*>(&a);
    auto* cb = dynamic_cast<COBB*>(&b);

    ContactManifold m;

    // ---- Sphere pairs ----
    if (sa && sb) {
        if (detectSphereSphere(*sa, *sb, m))
            pgsCollisionResponse(a, b, m, dt);
        return;
    }
    if (sa && ab) {
        // detect gives AABB(b)→sphere(a); flip to a→b
        if (detectSphereAABB(*sa, *ab, m)) {
            m.normal = -m.normal;
            pgsCollisionResponse(a, b, m, dt);
        }
        return;
    }
    if (aa && sb) {
        // detect gives AABB(a)→sphere(b); already a→b
        if (detectSphereAABB(*sb, *aa, m))
            pgsCollisionResponse(a, b, m, dt);
        return;
    }

    // ---- AABB pairs ----
    if (aa && ab) {
        if (detectAABBAABB(*aa, *ab, m))
            pgsCollisionResponse(a, b, m, dt);
        return;
    }

    // ---- OBB pairs ----
    // detectSphereOBB outputs "OBB→sphere"; flip when sphere is a (a→b needed)
    if (sa && cb) {
        if (detectSphereOBB(*sa, *cb, m)) {
            m.normal = -m.normal;
            pgsCollisionResponse(a, b, m, dt);
        }
        return;
    }
    if (ca && sb) {
        // OBB is a, sphere is b: detectSphereOBB(sphere_b, obb_a) → OBB(a)→sphere(b) = a→b
        if (detectSphereOBB(*sb, *ca, m))
            pgsCollisionResponse(a, b, m, dt);
        return;
    }
    // detectAABBOBB outputs a→b already
    if (aa && cb) {
        if (detectAABBOBB(*aa, *cb, m))
            pgsCollisionResponse(a, b, m, dt);
        return;
    }
    if (ca && ab) {
        // OBB is a, AABB is b: call with AABB as first arg → flip
        if (detectAABBOBB(*ab, *ca, m)) {
            m.normal = -m.normal;
            pgsCollisionResponse(a, b, m, dt);
        }
        return;
    }
    if (ca && cb) {
        if (detectOBBOBB(*ca, *cb, m))
            pgsCollisionResponse(a, b, m, dt);
        return;
    }
}

} // namespace physics::ColliderDiscrete
