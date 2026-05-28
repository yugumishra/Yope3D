#include "ColliderDiscrete.h"
#include "PhysicsConstants.h"
#include "../ecs/Registry.h"
#include "../ecs/Components.h"
#include "../world/Transform.h"
#include <cmath>
#include <algorithm>
#include <vector>

namespace physics::ColliderDiscrete {

// ============================================================================
// Global PGS (Projected Gauss-Seidel) solver — two-phase detect / solve.
// Phase 1: detect() appends an ActiveContact for each colliding pair.
// Phase 2: solveIsland() precomputes, warm-starts, iterates velocity
//          constraints, writes the cache, then runs split-impulse position pass.
// Normal convention: from a toward b throughout.
// ============================================================================

void solveIsland(std::vector<ActiveContact>& contacts, float dt,
                 ecs::Registry& reg, EntityContactCache& cache)
{
    auto getH  = [&](ecs::Entity e) -> ecs::Hull*   { return reg.get<ecs::Hull>(e); };
    auto getTf = [&](ecs::Entity e) -> Transform*   { return reg.get<Transform>(e); };
    auto isFixed = [&](ecs::Entity e) -> bool        { return reg.has<ecs::Fixed>(e); };

    // Apply accumulated impulses to velocity/omega; zero accumulators.
    auto applyImpulses = [&](ecs::Entity e) {
        auto* hc = getH(e);
        if (!hc) return;
        if (!isFixed(e)) {
            hc->velocity += hc->linearImpulse  * hc->inverseMass;
            hc->omega    += hc->inertiaTensorWorld * hc->angularImpulse;
        }
        hc->linearImpulse  = {};
        hc->angularImpulse = {};
    };

    // ---- Precompute ----
    for (auto& c : contacts) {
        auto* ha = getH(c.a);  auto* hb = getH(c.b);
        auto* tfa = getTf(c.a); auto* tfb = getTf(c.b);
        if (!ha || !hb || !tfa || !tfb) continue;

        math::Vec3 n = c.manifold.normal;
        c.T1 = (std::abs(n.x) < 0.9f)
             ? n.cross({1.0f, 0.0f, 0.0f})
             : n.cross({0.0f, 1.0f, 0.0f});
        float t1len = std::sqrt(c.T1.dot(c.T1));
        if (t1len > 1e-7f) c.T1 = c.T1 * (1.0f / t1len);
        c.T2  = n.cross(c.T1);
        c.mu  = std::sqrt(ha->friction * hb->friction);
        c.e   = std::sqrt(ha->restitution * hb->restitution);
        c.IinvA = ha->inertiaTensorWorld;
        c.IinvB = hb->inertiaTensorWorld;

        for (int i = 0; i < c.manifold.numContacts; i++) {
            c.rA[i] = c.manifold.contactPoints[i] - tfa->position;
            c.rB[i] = c.manifold.contactPoints[i] - tfb->position;

            math::Vec3 angA = (c.IinvA * c.rA[i].cross(n)).cross(c.rA[i]);
            math::Vec3 angB = (c.IinvB * c.rB[i].cross(n)).cross(c.rB[i]);
            float effN = ha->inverseMass + hb->inverseMass + angA.dot(n) + angB.dot(n);
            c.W[i] = (effN > 1e-6f) ? 1.0f / effN : 0.0f;

            math::Vec3 angT1A = (c.IinvA * c.rA[i].cross(c.T1)).cross(c.rA[i]);
            math::Vec3 angT1B = (c.IinvB * c.rB[i].cross(c.T1)).cross(c.rB[i]);
            float effT1 = ha->inverseMass + hb->inverseMass + angT1A.dot(c.T1) + angT1B.dot(c.T1);
            c.Wt1[i] = (effT1 > 1e-6f) ? 1.0f / effT1 : 0.0f;

            math::Vec3 angT2A = (c.IinvA * c.rA[i].cross(c.T2)).cross(c.rA[i]);
            math::Vec3 angT2B = (c.IinvB * c.rB[i].cross(c.T2)).cross(c.rB[i]);
            float effT2 = ha->inverseMass + hb->inverseMass + angT2A.dot(c.T2) + angT2B.dot(c.T2);
            c.Wt2[i] = (effT2 > 1e-6f) ? 1.0f / effT2 : 0.0f;

            math::Vec3 relVel0 = hb->velocity + hb->omega.cross(c.rB[i])
                               - ha->velocity - ha->omega.cross(c.rA[i]);
            float vn0 = relVel0.dot(n);
            c.neta[i] = (vn0 < -PGS_RESTITUTION_THRESHOLD) ? -c.e * vn0 : 0.0f;
        }
    }

    // ---- Warm start ----
    for (auto& c : contacts) {
        auto* ha = getH(c.a); auto* hb = getH(c.b);
        if (!ha || !hb) continue;
        math::Vec3 n = c.manifold.normal;
        for (int i = 0; i < c.manifold.numContacts; i++) {
            if (c.W[i] == 0.0f) continue;
            auto it = cache.find({c.a, c.b, i});
            if (it == cache.end()) continue;

            c.lambda[i]   = it->second.normal * 0.9f;
            c.lambdaT1[i] = it->second.t1     * 0.5f;
            c.lambdaT2[i] = it->second.t2     * 0.5f;
            float cone = c.mu * c.lambda[i];
            c.lambdaT1[i] = std::max(-cone, std::min(cone, c.lambdaT1[i]));
            c.lambdaT2[i] = std::max(-cone, std::min(cone, c.lambdaT2[i]));

            math::Vec3 imp = n * c.lambda[i] + c.T1 * c.lambdaT1[i] + c.T2 * c.lambdaT2[i];
            if (!isFixed(c.a)) { ha->linearImpulse += -imp; ha->angularImpulse += c.rA[i].cross(-imp); }
            if (!isFixed(c.b)) { hb->linearImpulse +=  imp; hb->angularImpulse += c.rB[i].cross( imp); }
            applyImpulses(c.a);
            applyImpulses(c.b);
        }
    }

    // ---- Velocity iterations (global) ----
    for (int iter = 0; iter < PGS_VELOCITY_ITERATIONS; iter++) {
        for (auto& c : contacts) {
            auto* ha = getH(c.a); auto* hb = getH(c.b);
            if (!ha || !hb) continue;
            math::Vec3 n = c.manifold.normal;

            for (int i = 0; i < c.manifold.numContacts; i++) {
                if (c.W[i] == 0.0f) continue;

                math::Vec3 relVel = hb->velocity + hb->omega.cross(c.rB[i])
                                  - ha->velocity - ha->omega.cross(c.rA[i]);

                // Normal
                float vn   = relVel.dot(n);
                float dL   = c.W[i] * -(vn - c.neta[i]);
                float oldL = c.lambda[i];
                c.lambda[i] = std::max(0.0f, c.lambda[i] + dL);
                float dl    = c.lambda[i] - oldL;
                if (std::abs(dl) > 1e-10f) {
                    math::Vec3 imp = n * dl;
                    if (!isFixed(c.a)) { ha->linearImpulse += -imp; ha->angularImpulse += c.rA[i].cross(-imp); }
                    if (!isFixed(c.b)) { hb->linearImpulse +=  imp; hb->angularImpulse += c.rB[i].cross( imp); }
                    applyImpulses(c.a);
                    applyImpulses(c.b);
                    relVel = hb->velocity + hb->omega.cross(c.rB[i])
                           - ha->velocity - ha->omega.cross(c.rA[i]);
                }

                // T1 friction
                if (c.mu > 0.0f && c.Wt1[i] > 0.0f) {
                    float dLt1  = -c.Wt1[i] * relVel.dot(c.T1);
                    float oldT1 = c.lambdaT1[i];
                    float cone  = c.mu * c.lambda[i];
                    c.lambdaT1[i] = std::max(-cone, std::min(cone, c.lambdaT1[i] + dLt1));
                    float dT1 = c.lambdaT1[i] - oldT1;
                    if (std::abs(dT1) > 1e-10f) {
                        math::Vec3 fImp = c.T1 * dT1;
                        if (!isFixed(c.a)) { ha->linearImpulse += -fImp; ha->angularImpulse += c.rA[i].cross(-fImp); }
                        if (!isFixed(c.b)) { hb->linearImpulse +=  fImp; hb->angularImpulse += c.rB[i].cross( fImp); }
                        applyImpulses(c.a);
                        applyImpulses(c.b);
                        relVel = hb->velocity + hb->omega.cross(c.rB[i])
                               - ha->velocity - ha->omega.cross(c.rA[i]);
                    }
                }

                // T2 friction
                if (c.mu > 0.0f && c.Wt2[i] > 0.0f) {
                    float dLt2  = -c.Wt2[i] * relVel.dot(c.T2);
                    float oldT2 = c.lambdaT2[i];
                    float cone  = c.mu * c.lambda[i];
                    c.lambdaT2[i] = std::max(-cone, std::min(cone, c.lambdaT2[i] + dLt2));
                    float dT2 = c.lambdaT2[i] - oldT2;
                    if (std::abs(dT2) > 1e-10f) {
                        math::Vec3 fImp = c.T2 * dT2;
                        if (!isFixed(c.a)) { ha->linearImpulse += -fImp; ha->angularImpulse += c.rA[i].cross(-fImp); }
                        if (!isFixed(c.b)) { hb->linearImpulse +=  fImp; hb->angularImpulse += c.rB[i].cross( fImp); }
                        applyImpulses(c.a);
                        applyImpulses(c.b);
                    }
                }
            }
        }
    }

    // ---- Cache write-back ----
    for (auto& c : contacts) {
        for (int i = 0; i < c.manifold.numContacts; i++) {
            if (c.W[i] == 0.0f) continue;
            cache[{c.a, c.b, i}] = {c.lambda[i], c.lambdaT1[i], c.lambdaT2[i]};
        }
    }

    // ---- Position iterations (split impulse) ----
    for (int iter = 0; iter < PGS_POSITION_ITERATIONS; iter++) {
        for (auto& c : contacts) {
            auto* ha = getH(c.a); auto* hb = getH(c.b);
            if (!ha || !hb) continue;
            math::Vec3 n = c.manifold.normal;
            for (int i = 0; i < c.manifold.numContacts; i++) {
                if (c.W[i] == 0.0f) continue;

                float cStarN = (hb->pseudoVel + hb->pseudoOmega.cross(c.rB[i])
                              - ha->pseudoVel - ha->pseudoOmega.cross(c.rA[i])).dot(n);
                float pseudoBias = (SPLIT_BETA / dt) * std::max(0.0f, c.manifold.depths[i] - SPLIT_SLOP);
                float dLp  = -c.W[i] * (cStarN - pseudoBias);
                float oldLp = c.lambdaP[i];
                c.lambdaP[i] = std::max(0.0f, c.lambdaP[i] + dLp);
                float dL = c.lambdaP[i] - oldLp;
                if (std::abs(dL) < 1e-10f) continue;

                math::Vec3 pImp = n * dL;
                if (!isFixed(c.a)) {
                    ha->pseudoVel   += -pImp * ha->inverseMass;
                    ha->pseudoOmega += c.IinvA * c.rA[i].cross(-pImp);
                }
                if (!isFixed(c.b)) {
                    hb->pseudoVel   +=  pImp * hb->inverseMass;
                    hb->pseudoOmega += c.IinvB * c.rB[i].cross( pImp);
                }
            }
        }
    }
}

// ============================================================================
// Sphere — Sphere   (normal convention: from a toward b)
// ============================================================================

bool detectSphereSphere(const SphereGeom& a, const SphereGeom& b, ContactManifold& m) {
    math::Vec3 diff   = b.getPosition() - a.getPosition();
    float      distSq = diff.dot(diff);
    float      rSum   = a.getRadius() + b.getRadius();
    if (distSq > rSum * rSum) return false;

    float dist = std::sqrt(distSq);
    if (dist < 1e-7f) {
        m.normal      = {1.0f, 0.0f, 0.0f};
        m.penetration = rSum;
    } else {
        m.normal      = diff * (1.0f / dist);
        m.penetration = rSum - dist;
    }
    m.numContacts      = 1;
    m.contactPoints[0] = a.getPosition() + m.normal * a.getRadius();
    m.depths[0]        = m.penetration;
    return true;
}

// ============================================================================
// Sphere — AABB   (normal convention: AABB → sphere; callers flip when sphere is 'a')
// ============================================================================

bool detectSphereAABB(const SphereGeom& sphere, const AABBGeom& aabb, ContactManifold& m) {
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
        m.normal      = diff * (1.0f / dist);
        m.penetration = r - dist;
        m.depths[0]   = m.penetration;
    } else {
        float depths[6] = {
            hi.x - sp.x, sp.x - lo.x,
            hi.y - sp.y, sp.y - lo.y,
            hi.z - sp.z, sp.z - lo.z
        };
        const math::Vec3 normals[6] = {
            {1,0,0},{-1,0,0},
            {0,1,0},{0,-1,0},
            {0,0,1},{0,0,-1}
        };
        float minDepth = depths[0];
        for (int i = 1; i < 6; i++)
            if (depths[i] < minDepth) minDepth = depths[i];

        math::Vec3 n = {};
        for (int i = 0; i < 6; i++)
            if (depths[i] <= minDepth + 1e-4f)
                n += normals[i];
        float len = std::sqrt(n.dot(n));
        n = (len > 1e-7f) ? n * (1.0f / len) : math::Vec3{0.0f, 1.0f, 0.0f};

        m.normal           = n;
        m.penetration      = r + minDepth;
        m.depths[0]        = m.penetration;
        m.contactPoints[0] = sp + n * minDepth;
    }
    return true;
}

// ============================================================================
// AABB — AABB   (normal convention: from a toward b)
// ============================================================================

bool detectAABBAABB(const AABBGeom& a, const AABBGeom& b, ContactManifold& m) {
    math::Vec3 posA = a.getPosition(), posB = b.getPosition();
    math::Vec3 eA   = a.getScales(),   eB   = b.getScales();

    float ovX = (eA.x + eB.x) - std::abs(posA.x - posB.x);
    float ovY = (eA.y + eB.y) - std::abs(posA.y - posB.y);
    float ovZ = (eA.z + eB.z) - std::abs(posA.z - posB.z);

    if (ovX <= 0.0f || ovY <= 0.0f || ovZ <= 0.0f) return false;

    int axis;
    if (ovX <= ovY && ovX <= ovZ) {
        m.normal      = {posA.x < posB.x ? 1.0f : -1.0f, 0.0f, 0.0f};
        m.penetration = ovX;
        axis = 0;
    } else if (ovY <= ovX && ovY <= ovZ) {
        m.normal      = {0.0f, posA.y < posB.y ? 1.0f : -1.0f, 0.0f};
        m.penetration = ovY;
        axis = 1;
    } else {
        m.normal      = {0.0f, 0.0f, posA.z < posB.z ? 1.0f : -1.0f};
        m.penetration = ovZ;
        axis = 2;
    }

    float loA[3] = {posA.x - eA.x, posA.y - eA.y, posA.z - eA.z};
    float hiA[3] = {posA.x + eA.x, posA.y + eA.y, posA.z + eA.z};
    float loB[3] = {posB.x - eB.x, posB.y - eB.y, posB.z - eB.z};
    float hiB[3] = {posB.x + eB.x, posB.y + eB.y, posB.z + eB.z};
    float lo[3], hi[3];
    for (int i = 0; i < 3; i++) {
        lo[i] = std::max(loA[i], loB[i]);
        hi[i] = std::min(hiA[i], hiB[i]);
    }

    int u = (axis + 1) % 3;
    int v = (axis + 2) % 3;
    float planeAxis = 0.5f * (lo[axis] + hi[axis]);
    bool uFlat = (hi[u] - lo[u]) < 1e-5f;
    bool vFlat = (hi[v] - lo[v]) < 1e-5f;
    float uVals[2] = {lo[u], hi[u]};
    float vVals[2] = {lo[v], hi[v]};
    int uN = uFlat ? 1 : 2;
    int vN = vFlat ? 1 : 2;
    if (uFlat) uVals[0] = 0.5f * (lo[u] + hi[u]);
    if (vFlat) vVals[0] = 0.5f * (lo[v] + hi[v]);

    int n = 0;
    for (int iu = 0; iu < uN; iu++) {
        for (int iv = 0; iv < vN; iv++) {
            math::Vec3 p{};
            float comp[3];
            comp[axis] = planeAxis;
            comp[u]    = uVals[iu];
            comp[v]    = vVals[iv];
            p.x = comp[0]; p.y = comp[1]; p.z = comp[2];
            m.contactPoints[n] = p;
            m.depths[n]        = m.penetration;
            n++;
        }
    }
    m.numContacts = n;
    return true;
}

// ============================================================================
// Sphere — OBB   (normal convention: OBB → sphere)
// ============================================================================

bool detectSphereOBB(const SphereGeom& sphere, const OBBGeom& obb, ContactManifold& m) {
    math::Vec3 sp  = sphere.getPosition();
    math::Vec3 op  = obb.getPosition();
    math::Vec3 ext = obb.getScales();
    float      r   = sphere.getRadius();

    math::Mat3 Rt    = obb.getRotTransform().transpose();
    math::Vec3 local = Rt * (sp - op);

    math::Vec3 clamped = {
        std::max(-ext.x, std::min(local.x, ext.x)),
        std::max(-ext.y, std::min(local.y, ext.y)),
        std::max(-ext.z, std::min(local.z, ext.z))
    };

    math::Vec3 worldClosest = op + obb.getRotTransform() * clamped;
    math::Vec3 diff         = sp - worldClosest;
    float      distSq       = diff.dot(diff);
    if (distSq >= r * r) return false;

    m.numContacts = 1;
    if (distSq > 1e-8f) {
        float dist        = std::sqrt(distSq);
        m.normal          = diff * (1.0f / dist);
        m.penetration     = r - dist;
        m.depths[0]       = m.penetration;
        m.contactPoints[0] = worldClosest;
    } else {
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

        m.normal          = obb.getRotTransform() * localNormals[best];
        m.penetration     = r + depths[best];
        m.depths[0]       = m.penetration;
        m.contactPoints[0] = sp - m.normal * r;
    }
    return true;
}

// ============================================================================
// AABB — OBB   15-axis SAT   (normal convention: from AABB (a) toward OBB (b))
// ============================================================================

bool detectAABBOBB(const AABBGeom& a, const OBBGeom& b, ContactManifold& m) {
    math::Vec3 aPos = a.getPosition(), aExt = a.getScales();
    math::Vec3 bPos = b.getPosition(), bExt = b.getScales();
    auto       bAxes = b.getOBBAxes();
    math::Vec3 diff  = bPos - aPos;

    const math::Vec3 worldAxes[3] = {{1,0,0},{0,1,0},{0,0,1}};
    float            extArr[3]    = {aExt.x, aExt.y, aExt.z};

    float      minOverlap = 1e30f;
    math::Vec3 bestAxis;
    int        axisType = 0;
    int        axisIdx  = 0;

    auto testAxis = [&](math::Vec3 n, int type, int idx) -> bool {
        float lenSq = n.dot(n);
        if (lenSq < 1e-8f) return true;
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

    math::Vec3 normal = (diff.dot(bestAxis) >= 0.0f) ? bestAxis : -bestAxis;
    m.normal      = normal;
    m.penetration = minOverlap;

    struct Cand { math::Vec3 pt; float depth; };

    if (axisType == 0) {
        float faceD   = aPos.dot(normal) + extArr[axisIdx];
        int   sa0     = (axisIdx + 1) % 3;
        int   sa1     = (axisIdx + 2) % 3;
        auto  corners = b.worldSpaceCorners();

        Cand cands[8]; int numCands = 0;
        for (auto& c : corners) {
            float depth = faceD - c.dot(normal);
            if (depth <= 0.0f) continue;
            float d0 = std::abs(c.dot(worldAxes[sa0]) - aPos.dot(worldAxes[sa0]));
            float d1 = std::abs(c.dot(worldAxes[sa1]) - aPos.dot(worldAxes[sa1]));
            if (d0 > extArr[sa0] || d1 > extArr[sa1]) continue;
            cands[numCands++] = {c, depth};
        }
        if (numCands == 0) {
            m.contactPoints[0] = (aPos + bPos) * 0.5f;
            m.depths[0]        = m.penetration;
            m.numContacts = 1;
        } else {
            for (int i = 1; i < numCands; i++) {
                Cand key = cands[i]; int j = i - 1;
                while (j >= 0 && cands[j].depth < key.depth) { cands[j+1] = cands[j]; j--; }
                cands[j+1] = key;
            }
            int k = std::min(numCands, 4);
            for (int i = 0; i < k; i++) {
                m.contactPoints[i] = cands[i].pt;
                m.depths[i]        = cands[i].depth;
            }
            m.numContacts = k;
        }
    } else if (axisType == 1) {
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
            m.depths[0]        = m.penetration;
            m.numContacts = 1;
        } else {
            for (int i = 1; i < numCands; i++) {
                Cand key = cands[i]; int j = i - 1;
                while (j >= 0 && cands[j].depth < key.depth) { cands[j+1] = cands[j]; j--; }
                cands[j+1] = key;
            }
            int k = std::min(numCands, 4);
            for (int i = 0; i < k; i++) {
                m.contactPoints[i] = cands[i].pt;
                m.depths[i]        = cands[i].depth;
            }
            m.numContacts = k;
        }
    } else {
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
        m.depths[0]        = m.penetration;
        m.numContacts = 1;
    }
    return true;
}

// ============================================================================
// OBB — OBB   15-axis SAT   (normal convention: from OBB a toward OBB b)
// ============================================================================

bool detectOBBOBB(const OBBGeom& a, const OBBGeom& b, ContactManifold& m) {
    math::Vec3 aPos = a.getPosition(), aExt = a.getScales();
    math::Vec3 bPos = b.getPosition(), bExt = b.getScales();
    auto       aAxes = a.getOBBAxes();
    auto       bAxes = b.getOBBAxes();
    math::Vec3 diff  = bPos - aPos;

    float      minOverlap = 1e30f;
    math::Vec3 bestAxis;
    int        axisType = 0;
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
            m.depths[0]        = m.penetration;
            m.numContacts = 1;
        } else {
            for (int i = 1; i < numCands; i++) {
                Cand key = cands[i]; int j = i - 1;
                while (j >= 0 && cands[j].depth < key.depth) { cands[j+1] = cands[j]; j--; }
                cands[j+1] = key;
            }
            int k = std::min(numCands, 4);
            for (int i = 0; i < k; i++) {
                m.contactPoints[i] = cands[i].pt;
                m.depths[i]        = cands[i].depth;
            }
            m.numContacts = k;
        }
    } else if (axisType == 1) {
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
            m.depths[0]        = m.penetration;
            m.numContacts = 1;
        } else {
            for (int i = 1; i < numCands; i++) {
                Cand key = cands[i]; int j = i - 1;
                while (j >= 0 && cands[j].depth < key.depth) { cands[j+1] = cands[j]; j--; }
                cands[j+1] = key;
            }
            int k = std::min(numCands, 4);
            for (int i = 0; i < k; i++) {
                m.contactPoints[i] = cands[i].pt;
                m.depths[i]        = cands[i].depth;
            }
            m.numContacts = k;
        }
    } else {
        math::Vec3 supportA = aPos;
        supportA = supportA + aAxes[0] * (normal.dot(aAxes[0]) >= 0.0f ?  aExt.x : -aExt.x);
        supportA = supportA + aAxes[1] * (normal.dot(aAxes[1]) >= 0.0f ?  aExt.y : -aExt.y);
        supportA = supportA + aAxes[2] * (normal.dot(aAxes[2]) >= 0.0f ?  aExt.z : -aExt.z);
        math::Vec3 supportB = bPos;
        supportB = supportB + bAxes[0] * (normal.dot(bAxes[0]) >= 0.0f ? -bExt.x :  bExt.x);
        supportB = supportB + bAxes[1] * (normal.dot(bAxes[1]) >= 0.0f ? -bExt.y :  bExt.y);
        supportB = supportB + bAxes[2] * (normal.dot(bAxes[2]) >= 0.0f ? -bExt.z :  bExt.z);
        m.contactPoints[0] = (supportA + supportB) * 0.5f;
        m.depths[0]        = m.penetration;
        m.numContacts = 1;
    }
    return true;
}

// ============================================================================
// detect() — ECS-based type dispatch. Reads positions and shapes from Registry.
// Normal convention: from a toward b.
// ============================================================================

void detect(ecs::Entity ea, ecs::Entity eb, ecs::Registry& reg,
            std::vector<ActiveContact>& contacts)
{
    auto* hca = reg.get<ecs::Hull>(ea);
    auto* hcb = reg.get<ecs::Hull>(eb);
    if (!hca || !hcb) return;
    if (!hca->tangible || !hcb->tangible) return;

    bool aFixed = reg.has<ecs::Fixed>(ea);
    bool bFixed = reg.has<ecs::Fixed>(eb);
    if (aFixed && bFixed) return;
    if (!(hca->collisionLayer & hcb->collisionMask) || !(hcb->collisionLayer & hca->collisionMask)) return;

    auto* tfa = reg.get<Transform>(ea);
    auto* tfb = reg.get<Transform>(eb);
    if (!tfa || !tfb) return;

    auto* sa = reg.get<ecs::SphereForm>(ea);
    auto* sb = reg.get<ecs::SphereForm>(eb);
    auto* aa = reg.get<ecs::AABBForm>(ea);
    auto* ab = reg.get<ecs::AABBForm>(eb);
    auto* ca = reg.get<ecs::OBBForm>(ea);
    auto* cb = reg.get<ecs::OBBForm>(eb);

    auto mSph  = [&](ecs::Entity e, float r) -> SphereGeom {
        return {reg.get<Transform>(e)->position, r};
    };
    auto mAABB = [&](ecs::Entity e, math::Vec3 ext) -> AABBGeom {
        return {reg.get<Transform>(e)->position, ext};
    };
    auto mOBB  = [&](ecs::Entity e, math::Vec3 ext) -> OBBGeom {
        auto* tf = reg.get<Transform>(e);
        return {tf->position, ext, math::Mat3::rotation(tf->rotation)};
    };

    ContactManifold m;
    auto push = [&](ecs::Entity pa, ecs::Entity pb, ContactManifold& cm) {
        ActiveContact c; c.a = pa; c.b = pb; c.manifold = cm;
        contacts.push_back(c);
    };

    if (sa && sb) { if (detectSphereSphere(mSph(ea,sa->radius), mSph(eb,sb->radius), m))    push(ea,eb,m); return; }
    if (sa && ab) { if (detectSphereAABB(mSph(ea,sa->radius), mAABB(eb,ab->extent), m))   { m.normal=-m.normal; push(ea,eb,m); } return; }
    if (aa && sb) { if (detectSphereAABB(mSph(eb,sb->radius), mAABB(ea,aa->extent), m))     push(ea,eb,m); return; }
    if (aa && ab) { if (detectAABBAABB(mAABB(ea,aa->extent), mAABB(eb,ab->extent), m))      push(ea,eb,m); return; }
    if (sa && cb) { if (detectSphereOBB(mSph(ea,sa->radius), mOBB(eb,cb->extent), m))    { m.normal=-m.normal; push(ea,eb,m); } return; }
    if (ca && sb) { if (detectSphereOBB(mSph(eb,sb->radius), mOBB(ea,ca->extent), m))      push(ea,eb,m); return; }
    if (aa && cb) { if (detectAABBOBB(mAABB(ea,aa->extent), mOBB(eb,cb->extent), m))       push(ea,eb,m); return; }
    if (ca && ab) { if (detectAABBOBB(mAABB(eb,ab->extent), mOBB(ea,ca->extent), m))     { m.normal=-m.normal; push(ea,eb,m); } return; }
    if (ca && cb) { if (detectOBBOBB(mOBB(ea,ca->extent), mOBB(eb,cb->extent), m))         push(ea,eb,m); return; }
}

} // namespace physics::ColliderDiscrete
