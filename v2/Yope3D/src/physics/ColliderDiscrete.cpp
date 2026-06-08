#include "ColliderDiscrete.h"
#include "PhysicsConstants.h"
#include "../ecs/Registry.h"
#include "../ecs/Components.h"
#include "../world/Transform.h"
#include "../debug/Profiler.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <chrono>
#include <variant>
#include <iostream>

namespace physics::ColliderDiscrete {

// ============================================================================
// Per-shape-pair narrowphase timing (A4).
// Each detect() call falls into one of 6 buckets (sph_sph, sph_aabb, sph_obb,
// aabb_aabb, aabb_obb, obb_obb). NPHASE_TIME(bucket) creates a thread-local
// RAII timer in debug builds; in release it expands to nothing.
// emitNarrowphaseProfile() pushes 6 records (even at count==0) so the CSV
// has a stable 6-row footprint per step for easy pandas pivot.
// ============================================================================
namespace {

enum PairBucket {
    NP_SPH_SPH   = 0,
    NP_SPH_AABB  = 1,
    NP_SPH_OBB   = 2,
    NP_AABB_AABB = 3,
    NP_AABB_OBB  = 4,
    NP_OBB_OBB   = 5,
    NP_BUCKETS   = 6,
};

constexpr const char* kBucketStage[NP_BUCKETS] = {
    "nphase_sph_sph",
    "nphase_sph_aabb",
    "nphase_sph_obb",
    "nphase_aabb_aabb",
    "nphase_aabb_obb",
    "nphase_obb_obb",
};

#ifndef NDEBUG
struct NarrowphaseTiming {
    double us[NP_BUCKETS] = {};
    int    n [NP_BUCKETS] = {};
};

thread_local NarrowphaseTiming g_npTiming;

struct PairTimer {
    PairBucket bucket;
    std::chrono::high_resolution_clock::time_point start;
    explicit PairTimer(PairBucket b)
        : bucket(b), start(std::chrono::high_resolution_clock::now()) {}
    ~PairTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        g_npTiming.us[bucket] +=
            std::chrono::duration<double, std::micro>(end - start).count();
        g_npTiming.n [bucket] += 1;
    }
};
#endif

} // anonymous

#ifndef NDEBUG
  #define NPHASE_TIME(bucket) PairTimer _np_timer_{bucket}
#else
  #define NPHASE_TIME(bucket) ((void)0)
#endif

void resetNarrowphaseTiming() {
#ifndef NDEBUG
    g_npTiming = {};
#endif
}

void emitNarrowphaseProfile() {
#ifndef NDEBUG
    for (int i = 0; i < NP_BUCKETS; ++i)
        YOPE_PROF_EMIT(kBucketStage[i], "physics", g_npTiming.us[i], g_npTiming.n[i]);
#endif
}

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
    // Takes the already-fetched Hull pointer to avoid a redundant registry lookup.
    auto applyImpulses = [](ecs::Hull* hc) {
        if (!hc) return;
        if (hc->inverseMass > 0.0f) {
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
            if (ha->inverseMass > 0.0f) { ha->linearImpulse += -imp; ha->angularImpulse += c.rA[i].cross(-imp); }
            if (hb->inverseMass > 0.0f) { hb->linearImpulse +=  imp; hb->angularImpulse += c.rB[i].cross( imp); }
            applyImpulses(ha);
            applyImpulses(hb);
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
                    if (ha->inverseMass > 0.0f) { ha->linearImpulse += -imp; ha->angularImpulse += c.rA[i].cross(-imp); }
                    if (hb->inverseMass > 0.0f) { hb->linearImpulse +=  imp; hb->angularImpulse += c.rB[i].cross( imp); }
                    applyImpulses(ha);
                    applyImpulses(hb);
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
                        if (ha->inverseMass > 0.0f) { ha->linearImpulse += -fImp; ha->angularImpulse += c.rA[i].cross(-fImp); }
                        if (hb->inverseMass > 0.0f) { hb->linearImpulse +=  fImp; hb->angularImpulse += c.rB[i].cross( fImp); }
                        applyImpulses(ha);
                        applyImpulses(hb);
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
                        if (ha->inverseMass > 0.0f) { ha->linearImpulse += -fImp; ha->angularImpulse += c.rA[i].cross(-fImp); }
                        if (hb->inverseMass > 0.0f) { hb->linearImpulse +=  fImp; hb->angularImpulse += c.rB[i].cross( fImp); }
                        applyImpulses(ha);
                        applyImpulses(hb);
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
// Sphere — Sphere support
// ============================================================================

math::Vec3 supportSphereSphere(const SphereGeom& a, const SphereGeom& b, const math::Vec3& dir) {
    //for any sphere, the farthest point in a direction is simply that direction (scaled by radius)
    //math::Vec3 supportA = a.getPosition() + dir * a.getRadius();
    //math::Vec3 supportB = b.getPosition() - dir * b.getRadius(); //invert direction for minkowski difference support calculation
    //below code is factored version of above
    return a.getPosition() - b.getPosition() + (dir.normalize()) * (a.getRadius() + b.getRadius());
}

// ============================================================================
// Sphere — AABB support
// ============================================================================

math::Vec3 supportSphereAABB(const SphereGeom& a, const AABBGeom& b, const math::Vec3& dir) {
    //for any sphere, the farthest point in a direction is simply that direction (scaled by radius)
    math::Vec3 knowns = a.getPosition() + dir.normalize() * a.getRadius()        - b.getPosition(); //include b.getposition now


    //for box shapes, multiple points can be the same furthest distance away
    //in this case their centroid must be computed
    //but since this is an aabb, this is easy we can use the sign of the direction vector itself
    return knowns + (~dir).hadamard(b.getScales());
    //hadamard (element wise) product takes care of any and all comparisons
    //any cases with multiple same distance points <==> one of the dir axes is 0 <==> contribution canceled out by zpp
    //ex: direction is simply x axis <==> yz = 0 for dir <==> contribution is x only (sign already determined by sign of dir.x) (this is what we want, no yz contribution/elements)
}


// ============================================================================
// Sphere — OBB support
// ============================================================================

math::Vec3 supportSphereOBB(const SphereGeom& a, const OBBGeom& b, const math::Vec3& dir) {
    //for any sphere, the farthest point in a direction is simply that direction (scaled by radius)
    math::Vec3 knowns = a.getPosition() + dir.normalize() * a.getRadius()        - b.getPosition(); //include b.getposition now


    //for box shapes, multiple points can be the same furthest distance away
    //in this case their centroid must be computed
    //for obbs this is a bit more complex but we can do it in obb space to reuse aabb technique
    //thankfully rotation matrices are orthonormal so their inverse is their transpose (makes this computationally light)
    math::Mat3 inv = b.getRotTransform().transpose();
    math::Vec3 local = inv * dir;
    //do aabb computation but in local, obb, space (b.getscales also local)
    local = (~local).hadamard(b.getScales());
    return knowns + b.getRotTransform() * local;
    //hadamard (element wise) product takes care of any and all comparisons
    //any cases with multiple same distance points <==> one of the dir axes is 0 <==> contribution canceled out by zpp
    //ex: direction is simply x axis <==> yz = 0 for dir <==> contribution is x only (sign already determined by sign of dir.x) (this is what we want, no yz contribution/elements)
}

// ============================================================================
// AABB — AABB support
// ============================================================================


math::Vec3 supportAABBAABB(const AABBGeom& a, const AABBGeom& b, const math::Vec3& dir) {
    //using the above defined aabb support technique, this can be a oneliner (with some factoring)
    return a.getPosition() - b.getPosition() + (~dir).hadamard(a.getScales() + b.getScales());
}

// ============================================================================
// AABB — OBB support
// ============================================================================


math::Vec3 supportAABBOBB(const AABBGeom& a, const OBBGeom& b, const math::Vec3& dir) {
    //compute both in wordl space and THEN add
    //use aabb technique
    math::Vec3 knowns = a.getPosition() + (~dir).hadamard(a.getScales())        - b.getPosition(); //include b.getposition now

    //for box shapes, multiple points can be the same furthest distance away
    //in this case their centroid must be computed
    //for obbs this is a bit more complex but we can do it in obb space to reuse aabb technique
    //thankfully rotation matrices are orthonormal so their inverse is their transpose (makes this computationally light)
    math::Mat3 inv = b.getRotTransform().transpose();
    math::Vec3 local = inv * dir;
    //do aabb computation but in local, obb, space (b.getscales also local)
    local = (~local).hadamard(b.getScales());
    return knowns + b.getRotTransform() * local;
    //hadamard (element wise) product takes care of any and all comparisons
    //any cases with multiple same distance points <==> one of the dir axes is 0 <==> contribution canceled out by zpp
    //ex: direction is simply x axis <==> yz = 0 for dir <==> contribution is x only (sign already determined by sign of dir.x) (this is what we want, no yz contribution/elements)
}

// ============================================================================
// OBB — OBB support
// ============================================================================


math::Vec3 supportOBBOBB(const OBBGeom& a, const OBBGeom& b, const math::Vec3& dir) {
    math::Vec3 support = a.getPosition() - b.getPosition();
    //use the obb technique twice (no other way than to just convert to world space for both)
    math::Mat3 invA = a.getRotTransform().transpose();
    math::Vec3 localA = invA * dir;
    //this is the point selection (aabb style but in obb space so ti works)
    localA = (~localA).hadamard(a.getScales());
    //back to world
    support += a.getRotTransform() * localA;

    math::Mat3 invB = b.getRotTransform().transpose();
    math::Vec3 localB = invB * dir;
    //this is the point selection (aabb style but in obb space so ti works)
    localB = (~localB).hadamard(b.getScales());
    //back to world and add both together
    return support + b.getRotTransform() * localB;
}

// Unified overload set — lets std::visit dispatch by type automatically
math::Vec3 support(const SphereGeom& a, const SphereGeom& b, const math::Vec3& d) { return  supportSphereSphere(a, b, d); }
math::Vec3 support(const SphereGeom& a, const AABBGeom&   b, const math::Vec3& d) { return  supportSphereAABB(a, b, d);   }
math::Vec3 support(const SphereGeom& a, const OBBGeom&    b, const math::Vec3& d) { return  supportSphereOBB(a, b, d);    }
math::Vec3 support(const AABBGeom&   a, const SphereGeom& b, const math::Vec3& d) { return -supportSphereAABB(b, a, -d);  } // flip
math::Vec3 support(const AABBGeom&   a, const AABBGeom&   b, const math::Vec3& d) { return  supportAABBAABB(a, b, d);     }
math::Vec3 support(const AABBGeom&   a, const OBBGeom&    b, const math::Vec3& d) { return  supportAABBOBB(a, b, d);      }
math::Vec3 support(const OBBGeom&    a, const SphereGeom& b, const math::Vec3& d) { return -supportSphereOBB(b, a, -d);   } // flip
math::Vec3 support(const OBBGeom&    a, const AABBGeom&   b, const math::Vec3& d) { return -supportAABBOBB(b, a, -d);     } // flip
math::Vec3 support(const OBBGeom&    a, const OBBGeom&    b, const math::Vec3& d) { return  supportOBBOBB(a, b, d);       }

auto makeSupport(const ColliderDiscrete::ShapeVariant& va, const ColliderDiscrete::ShapeVariant& vb){
    return [&va, &vb](const math::Vec3& dir) -> math::Vec3 {
        return std::visit([&dir](const auto& a, const auto& b) {
            return support(a, b, dir);
        }, va, vb);
    };
}

math::Vec3 shapePosition(const ShapeVariant& v) {
    return std::visit([](const auto& s) { return s.getPosition(); }, v);
}

ShapeVariant makeShapeVariant(ecs::Entity& e, ecs::Registry& reg)  {
    auto* tf = reg.get<Transform>(e);
    if (auto* s = reg.get<ecs::SphereForm>(e))
        return SphereGeom{tf->position, s->radius};
    if (auto* a = reg.get<ecs::AABBForm>(e))
        return AABBGeom{tf->position, a->extent};
    if (auto* c = reg.get<ecs::OBBForm>(e))
        return OBBGeom{tf->position, c->extent, math::Mat3::rotation(tf->rotation)};
    return SphereGeom{tf->position, 0.0f}; // fallback
}

PairBucket getBucket(int ai, int bi) {
    // normalize so ai <= bi (sph_aabb == aabb_sph bucket-wise)
    if (ai > bi) std::swap(ai, bi);
    // (ai, bi) -> bucket
    // (0,0)=sph_sph, (0,1)=sph_aabb, (0,2)=sph_obb
    //                (1,1)=aabb_aabb, (1,2)=aabb_obb
    //                                 (2,2)=obb_obb
    constexpr PairBucket table[3][3] = {
        { NP_SPH_SPH,  NP_SPH_AABB, NP_SPH_OBB  },
        { NP_SPH_AABB, NP_AABB_AABB,NP_AABB_OBB  },
        { NP_SPH_OBB,  NP_AABB_OBB, NP_OBB_OBB  },
    };
    return table[ai][bi];
}


void detectGJK(ecs::Entity ea, ecs::Entity eb, ecs::Registry& reg,
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

    ShapeVariant va = makeShapeVariant(ea, reg);
    ShapeVariant vb = makeShapeVariant(eb, reg);

    //determine bucket for profiling
    PairBucket bucket = getBucket(va.index(), vb.index());
    NPHASE_TIME(bucket);

    //determine the appropriate support function
    auto supportFunction = makeSupport(va, vb);

    //instantiate the manifold and simplex in which gjk results will bes tored
    ContactManifold m;
    ColliderDiscrete::GJKSimplex simplex;
    //compute the initial direction for best convergence to be the difference of the 2 objects
    math::Vec3 initDir = shapePosition(vb) - shapePosition(va);
    //fallback if 0 to just basic down
    float      lenSq   = initDir.dot(initDir);
    if (lenSq < 1e-8f) initDir = {0.0f, -1.0f, 0.0f}; // coincident centers fallback
    
    //detect with gjk
    if (!gjkIntersect(supportFunction, simplex, initDir)) return;
    
    //make the manifold with epa from gjk result
    epaManifold(supportFunction, simplex, m);
}

template<typename SupportFn>
bool gjkIntersect(SupportFn&& support, GJKSimplex& simplex, math::Vec3 initDir, GJKTrace* trace) {
    //get a point on the minkowski difference using the support function (any point will do but we can use the support function and the initial direction to improve convergence)
    math::Vec3 initialPoint = support(initDir);
    //add to the simplex as the first point
    simplex.points[simplex.n++] = initialPoint;
    //now we need to trap the origin so we will set our initial origin to opposite the support (so we move back towards the origin)
    math::Vec3 dir = -initialPoint;

    // [trace] seed frame — observational only, no effect on the algorithm.
    if (trace) trace->push_back({initDir, initialPoint, initialPoint.dot(initDir),
                                 simplex.n,
                                 {simplex.points[0], simplex.points[1], simplex.points[2], simplex.points[3]},
                                 false, false});

    //gjk main loop (bounded to prevent infinite recursion in cases where fp precision can cause it)
    for(int i = 0; i< MAX_GJK_ITERATIONS; i++) {
        //check for a nonzero direction
        if(dir.dot(dir) < GJK_EPS) dir = initDir;

        //get the support in the direction
        math::Vec3 usedDir = dir;                 // [trace] dir actually used (post guard)
        math::Vec3 supportPoint = support(dir);
        float dotSD = supportPoint.dot(dir);

        //check for the case where the minkowski diff won't contain the origin
        if(dotSD < GJK_EPS) {
            //moved as far as we could in the direction of the origin but we didn't move past it
            //ergo the difference CANNOT contain the origin <==> not intersecting
            if (trace) trace->push_back({usedDir, supportPoint, dotSD, simplex.n,
                                         {simplex.points[0], simplex.points[1], simplex.points[2], simplex.points[3]},
                                         false, true});
            
            
            return false;
        }

        //push the new point onto the simplex (this push scheme means the oldest point is lower than the newer point)
        simplex.points[simplex.n++] = supportPoint;

        //now update/reform the simplex with the origin trapping points only (voronoi region logic)
        bool early = updateSimplex(simplex, dir);

        // [trace] iteration frame — captured AFTER updateSimplex (dir is now the next dir,
        // so we record usedDir, the direction this support point was taken along).
        if (trace) trace->push_back({usedDir, supportPoint, dotSD, simplex.n,
                                     {simplex.points[0], simplex.points[1], simplex.points[2], simplex.points[3]},
                                     early, false});

        //update simplex can also find early origin detections so check that here as well
        //check if it formed a tetrahedron containing the origin
        if(early || simplex.n == 4) {
            //it did, we have an intersection, return true (simplex passed by reference so epa can use the simplex)
            return true;
        }
    }
    //default behavior to not include (avoids extra narrowphase computation)
    return false;
}

//both the simplex and the search direction need to be updated so pass those both in by reference
bool updateSimplex(GJKSimplex& simplex, math::Vec3& direction) {
    //note: points are described in reverse addition order
    //further down the alphabet means older
    //so b got added before a, after c
    switch(simplex.n) {
        case 2: {
            //we have 2 points, oldest one in b.
            //we need to determine which region the origin is in
            //since a was in the direction of the origin from 0, this simplifies hte check
            //we only need to compare whether the ray ab is in the direction of the origin or not

            //"in the direction of the origin" <==> ab * (ao <==> -a) >0
            math::Vec3 ao = -simplex.points[1];
            math::Vec3 ab = simplex.points[0] - simplex.points[1];
            
            if(ab.dot(ao) > 0) {
                //origin contained witihn the 2 planes defined by a and b
                //simplex already has a & b so only thing left is dircetion computation
                //here use triple cross product (ab x ao x ab) to get a vector that is:
                // 1 perpendicular to ab (we don't want to search in a redundant direction, ab already covered)
                // 2 in the plane of ab and ao
                math::Vec3 aCrossb = ab.cross(ao);
                if(aCrossb.dot(aCrossb) < GJK_EPS) {
                    
                    //we have colinear points
                    //check if the origin is inbetween
                    if(simplex.points[0].dot(simplex.points[1]) <= 0) {
                        //origin in between
                        return true;
                    }else {
                        //origin past a (update simplex to only have a)
                        simplex.points[0] = simplex.points[1]; //zeroing simplex.points[1] not necessary as any pushes will ovewrite it 
                        simplex.n = 1;
                        // (just n needs to be updated to reflect the true length)

                        //direction is just ao
                        direction = ao;
                        return false;
                    }
                }
                direction = (aCrossb).cross(ab);
                return false;
            }else {
                //origin past a (update simplex to only have a)
                simplex.points[0] = simplex.points[1]; //zeroing simplex.points[1] not necessary as any pushes will ovewrite it 
                simplex.n = 1;
                // (just n needs to be updated to reflect the true length)

                //direction is just ao
                direction = ao;
                return false;
            }
        }
        case 3: {
            //first compute triangle normal as ab cross ac
            math::Vec3 ao = -simplex.points[2];
            math::Vec3 ab = simplex.points[1] - simplex.points[2];
            math::Vec3 ac = simplex.points[0] - simplex.points[2];

            math::Vec3 triangleNormal = ab.cross(ac);

            //ab line already tested, so it has to be past there
            //so we test one of the other edges, abc x ac or ab x abc

            math::Vec3 edge1 = triangleNormal.cross(ac);

            if(edge1.dot(ao) > 0) {
                //past the edge of the triangle, need to determine if its past a or not
                if(ac.dot(ao) > 0) {
                    //it is, simplex is ac
                    simplex.points[1] = simplex.points[2]; //zeroing simplex.points[2] not necessary as any pushes will ovewrite it 
                    simplex.n = 2;

                    //search direction is the same as the line case for 2 points (ac x ao x ac)
                    direction = (ac.cross(ao)).cross(ac);
                    return false;
                }else {
                    //behind ac, must test if behind b as well (some weird triangles can cause this)
                    if(ab.dot(ao) > 0) {
                        //within the region bound by a and b (simplex is a and b)
                        
                        simplex.points[0] = simplex.points[1];
                        simplex.points[1] = simplex.points[2];
                        simplex.n--; //zeroing simplex.points[2] not necessary as any pushes will ovewrite it 

                        //do usual line calculation
                        direction = (ab.cross(ao)).cross(ab);
                        return false;
                    }else {
                        //behind a for sure (tested both edges)
                        //simplex is simply a now
                        simplex.points[0] = simplex.points[2];
                        simplex.n = 1; //zeroing simplex.points[1/2] not necessary as any pushes will overwrite it

                        direction = ao;
                        return false;
                    }
                }
            }else {
                math::Vec3 edge2 = ab.cross(triangleNormal);
                //now test to see if beyond the other edge
                if(edge2.dot(ao) > 0) {
                    //we are indeed beyond the other edge, now test beyond/behind ab (same case as else above)
                    if(ab.dot(ao) > 0) {
                        //within the region bound by a and b (simplex is a and b)
                        
                        simplex.points[0] = simplex.points[1];
                        simplex.points[1] = simplex.points[2];
                        simplex.n--; //zeroing simplex.points[2] not necessary as any pushes will ovewrite it 

                        //do usual line calculation
                        direction = (ab.cross(ao)).cross(ab);
                        return false;
                    }else {
                        //behind a for sure (tested both edges)
                        //simplex is simply a now
                        simplex.points[0] = simplex.points[2];
                        simplex.n = 1; //zeroing simplex.points[1/2] not necessary as any pushes will overwrite it

                        direction = ao;
                        return false;
                    }
                }else {
                    //within both edges, origin must be above or below triangle
                    //test via triangle normal now
                    if(triangleNormal.dot(ao) > 0) {
                        //above the triangle, winding is correct
                        //update direction with new search direction
                        direction = triangleNormal;
                        return false;
                    }else {
                        //reverse winding (swap b and c)
                        math::Vec3 temp = simplex.points[0];
                        simplex.points[0] = simplex.points[1];
                        simplex.points[1] = temp;

                        //flip the direction since the origin is below
                        direction = -triangleNormal;
                        return false;
                    }
                }
            }
            break;
        }
        case 4: {
            //treat this as a series of triangle tests
            //in general, a tetrahedron divides 3d space into 15 regions (one interior, 4 faces, 6 edges, 4 points)
            //however, results can be reused since we know the triangle build order and the tests that were used to find the points
            //ex: testing the triangle normal of dcb is redundant since that is how point a got added

            //always compare with ao so compute here
            math::Vec3 ao = -simplex.points[3];

            //the one with the least apriori info is triangle abc so start with that test
            math::Vec3 ab = simplex.points[2] - simplex.points[3];
            math::Vec3 ac = simplex.points[1] - simplex.points[3];

            //find by crossing (note we always want normals to point into the tetrahedron 
            // so we cross in the order that produces the vector towards the missing vertex 
            // ex: (acb -> ac x ab poitns towards d)))
            //this means ABOVE a plane (in this tetrahedral case) means into the interior and vice versa
            math::Vec3 abcNormal = ac.cross(ab);

            if(abcNormal.dot(ao) > 0) {
                //inside/above the abc plane
                
                //now test abd plane (plane with 2nd least info)
                math::Vec3 ad = simplex.points[0] - simplex.points[3];

                //follow inward normal convention
                math::Vec3 abdNormal = ab.cross(ad);

                if(abdNormal.dot(ao) > 0) {
                    //inside/above abd plane

                    //now test against acd plane (plane with the most info)
                    math::Vec3 acdNormal = ad.cross(ac);
                    if(acdNormal.dot(ao) > 0) {
                        //within all 3 planes woo hoo
                        return false;
                    }else {
                        //outside this plane, check both edges
                        math::Vec3 edge1 = acdNormal.cross(ac);
                        if(edge1.dot(ao) > 0) {
                            //beyond this edge, check if behind a in the dir of c
                            if(ac.dot(ao) > 0) {
                                //c check redundant so
                                //in between a and c
                                simplex.points[0] = simplex.points[1];
                                simplex.points[1] = simplex.points[3];
                                simplex.n = 2;
                                
                                //do line simplex dir calc
                                direction = (ac.cross(ao)).cross(ac);
                                return false;
                            }else {
                                //behind a in the dir of c, now check if behind a in the dir of d
                                if(ad.dot(ao) > 0) {
                                    //d check redundant so
                                    //in between a and d
                                    simplex.points[1] = simplex.points[3];
                                    simplex.n = 2;

                                    //do line simplex dir calc
                                    direction = (ad.cross(ao)).cross(ad);
                                    return false;
                                }else {
                                    //behind a in both dirs, so only point is a
                                    simplex.points[0] = simplex.points[3];
                                    simplex.n = 1;

                                    direction = ao;
                                    return false;
                                }
                            }
                        }else {
                            //within this edge, now chec kif beyond the other edge
                            math::Vec3 edge2 = ad.cross(acdNormal);
                            if(edge2.dot(ao) > 0) {
                                //beyond this edge too, check if behind a in the dir of d
                                //behind a in the dir of c, now check if behind a in the dir of d
                                if(ad.dot(ao) > 0) {
                                    //d check redundant so
                                    //in between a and d
                                    simplex.points[1] = simplex.points[3];
                                    simplex.n = 2;

                                    //do line simplex dir calc
                                    direction = (ad.cross(ao)).cross(ad);
                                    return false;
                                }else {
                                    //behind a in both dirs, so only point is a
                                    simplex.points[0] = simplex.points[3];
                                    simplex.n = 1;

                                    direction = ao;
                                    return false;
                                }
                            }else {
                                //within both edges and we checked the plane already so outside
                                //reverse c and d
                                math::Vec3 temp = simplex.points[0];
                                simplex.points[0] = simplex.points[1];
                                simplex.points[1] = temp;
                                simplex.points[2] = simplex.points[3]; //put a in third place
                                simplex.n = 3;

                                //dir is -acd
                                direction = -acdNormal;
                                return false;
                            }
                        }
                    }
                }else {
                    //below/outside of abd plane

                    //test first against an edge
                    math::Vec3 edge1 = ab.cross(abdNormal);

                    if(edge1.dot(ao) > 0) {
                        //past this edge
                        //check ab to see if its behind a in the dir of b
                        if(ab.dot(ao) > 0) {
                            //also check b since we have NEVER checked behind the third point ever in the building of the simplex
                            //same sign optimization (flip check sign to avoid computing negative of vector)
                            if(simplex.points[2].dot(ao) < 0) {
                                //closest 2 points are ab
                                simplex.points[0] = simplex.points[2];
                                simplex.points[1] = simplex.points[3];
                                simplex.n = 2;

                                //do same direction calc
                                direction = (ab.cross(ao)).cross(ab);
                                return false;
                            }else {
                                //closest point is b only
                                simplex.points[0] = simplex.points[2];
                                simplex.n = 1;

                                //direction is simply towards the origin
                                direction = -simplex.points[0];
                                return false;
                            }
                        }else {
                            //behind a in the direction of b, now check if behind in the direction of d
                            if(ad.dot(ao) > 0) {
                                //d check already done (literally the first step in buliding the simplex)
                                //so only 2 points are a and d
                                simplex.points[1] = simplex.points[3];
                                simplex.n = 2;

                                //same line simplex direction computation
                                direction = (ad.cross(ao)).cross(ad);
                                return false;
                            }else {
                                //checked behind both edges, only point is a
                                simplex.points[0] = simplex.points[3];
                                simplex.n = 1;

                                //just go toward the origin
                                direction = ao;
                                return false;
                            }
                        }
                    }else {
                        //within this edge, check the other edge
                        math::Vec3 edge2 = abdNormal.cross(ad);

                        if(edge2.dot(ao) > 0) {
                            //beyond this edge as well, check if within ad (d check not needed)
                            //behind a in the direction of b, now check in the direction of d
                            if(ad.dot(ao) > 0) {
                                //d check already done (literally the first step in buliding the simplex)
                                //so only 2 points are a and d
                                simplex.points[1] = simplex.points[3];
                                simplex.n = 2;

                                //same line simplex direction computation
                                direction = (ad.cross(ao)).cross(ad);
                                return false;
                            }else {
                                //checked behind both edges, only point is a
                                simplex.points[0] = simplex.points[3];
                                simplex.n = 1;

                                //just go toward the origin
                                direction = ao;
                                return false;
                            }
                        }else {
                            //within this edge as well, and we tested against the plane already so it must be pointing outward
                            //reverse orientation
                            simplex.points[1] = simplex.points[2]; //b goes to c's spot
                            simplex.points[2] = simplex.points[3]; //a stays a
                            simplex.n = 3;

                            //direction is -abd
                            direction = -abdNormal;
                            return false;
                        }
                    }
                }
            }else {
                //below/outside abc plane
                //due to the lack of tests on the third point (b), some more tests need to be added post c and b (those voronoi regions haven't been tested)

                //first test against edge (good thing we already computed for acb normal)
                math::Vec3 edge1 = ac.cross(abcNormal);
                if(edge1.dot(ao) > 0) {
                    //past this edge
                    //test if within ac or not
                    //note: do NOT need to check past c because that test is redudant from past tests/simplex building algo
                    if(ac.dot(ao) > 0) {
                        //within the edge, simplex is ac only
                        simplex.points[0] = simplex.points[1];
                        simplex.points[1] = simplex.points[3];
                        simplex.n = 2; //zeroing not necessary, just set n = 2 (overwrites)

                        //direction is same as line case for simplex
                        direction = ac.cross(ao).cross(ac);
                        return false;
                    }else {
                        //check ab to see if its behind a in the dir of b
                        if(ab.dot(ao) > 0) {
                            //also check b since we have NEVER checked behind the third point ever in the building of the simplex
                            //same sign optimization (flip check sign to avoid computing negative of number)
                            if(simplex.points[2].dot(ao) < 0) {
                                //closest 2 points are ab
                                simplex.points[0] = simplex.points[2];
                                simplex.points[1] = simplex.points[3];
                                simplex.n = 2;

                                //do same direction calc
                                direction = (ab.cross(ao)).cross(ab);
                                return false;
                            }else {
                                //closest point is b only
                                simplex.points[0] = simplex.points[2];
                                simplex.n = 1;

                                //direction is simply towards the origin
                                direction = -simplex.points[0];
                                return false;
                            }
                        }else {
                            //its behind a on both ab and ac directions, so a only
                            simplex.points[0] = simplex.points[3];
                            simplex.n = 1;

                            //direction is simply towards the origin
                            direction = -simplex.points[0];

                            return false;
                        }
                    }
                }else {
                    //within the edge
                    //test the other edge to see if within the triangle
                    math::Vec3 edge2 = abcNormal.cross(ab);

                    if(edge2.dot(ao) > 0) {
                        //beyond this edge, do the same ab checks
                        //check ab to see if its behind a in the dir of b
                        if(ab.dot(ao) > 0) {
                            //also check b since we have NEVER checked behind the third point ever in the building of the simplex
                            //same sign optimization (flip check sign to avoid computing negative of number)
                            if(simplex.points[2].dot(ao) < 0) {
                                //closest 2 points are ab
                                simplex.points[0] = simplex.points[2];
                                simplex.points[1] = simplex.points[3];
                                simplex.n = 2;

                                //do same direction calc
                                direction = (ab.cross(ao)).cross(ab);
                                return false;
                            }else {
                                //closest point is b only
                                simplex.points[0] = simplex.points[2];
                                simplex.n = 1;

                                //direction is simply towards the origin
                                direction = -simplex.points[0];
                                return false;
                            }
                        }else {
                            //its behind a on both ab and ac directions, so a only
                            simplex.points[0] = simplex.points[3];
                            simplex.n = 1;

                            //direction is simply towards the origin
                            direction = -simplex.points[0];
                            return false;
                        }
                    }else {
                        //within both edges, and the very first check we did was plane normal
                        //we are outside/below the plane
                        //so winding is incorrect
                        simplex.points[0] = simplex.points[2]; //b becomes oldest
                        //c remains in place (swap functions this way)
                        simplex.points[2] = simplex.points[3]; //a becomes the newest
                        simplex.n = 3;

                        //direciton is the negative (since we need to look the other way
                        direction = -abcNormal;
                        return false;
                    }
                }
            }
            break;
        }
    }

    return false;
}

template<typename SupportFn>
bool epaManifold(SupportFn&& support, GJKSimplex& simplex, ContactManifold& m) {
    return false;
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

    if (sa && sb) { NPHASE_TIME(NP_SPH_SPH);   if (detectSphereSphere(mSph(ea,sa->radius), mSph(eb,sb->radius), m))    push(ea,eb,m); return; }
    if (sa && ab) { NPHASE_TIME(NP_SPH_AABB);  if (detectSphereAABB(mSph(ea,sa->radius), mAABB(eb,ab->extent), m))   { m.normal=-m.normal; push(ea,eb,m); } return; }
    if (aa && sb) { NPHASE_TIME(NP_SPH_AABB);  if (detectSphereAABB(mSph(eb,sb->radius), mAABB(ea,aa->extent), m))     push(ea,eb,m); return; }
    if (aa && ab) { NPHASE_TIME(NP_AABB_AABB); if (detectAABBAABB(mAABB(ea,aa->extent), mAABB(eb,ab->extent), m))      push(ea,eb,m); return; }
    if (sa && cb) { NPHASE_TIME(NP_SPH_OBB);   if (detectSphereOBB(mSph(ea,sa->radius), mOBB(eb,cb->extent), m))    { m.normal=-m.normal; push(ea,eb,m); } return; }
    if (ca && sb) { NPHASE_TIME(NP_SPH_OBB);   if (detectSphereOBB(mSph(eb,sb->radius), mOBB(ea,ca->extent), m))      push(ea,eb,m); return; }
    if (aa && cb) { NPHASE_TIME(NP_AABB_OBB);  if (detectAABBOBB(mAABB(ea,aa->extent), mOBB(eb,cb->extent), m))       push(ea,eb,m); return; }
    if (ca && ab) { NPHASE_TIME(NP_AABB_OBB);  if (detectAABBOBB(mAABB(eb,ab->extent), mOBB(ea,ca->extent), m))     { m.normal=-m.normal; push(ea,eb,m); } return; }
    if (ca && cb) { NPHASE_TIME(NP_OBB_OBB);   if (detectOBBOBB(mOBB(ea,ca->extent), mOBB(eb,cb->extent), m))         push(ea,eb,m); return; }
}

// ============================================================================
// GJK debug / test harness (editor oracle + simplex stepper).
// Defined here, AFTER gjkIntersect/updateSimplex, so the templated intersect is
// already visible for instantiation. None of this is GJK logic — it only wraps
// the existing pieces so editor tooling can drive the real algorithm and diff it
// against the proven SAT detect* routines.
// ============================================================================

std::function<math::Vec3(math::Vec3)>
makeSupportFn(ecs::Entity ea, ecs::Entity eb, ecs::Registry& reg) {
    // Capture the resolved shapes BY VALUE: the returned closure outlives this
    // call, unlike makeSupport()'s by-reference capture used inside detect().
    ShapeVariant va = makeShapeVariant(ea, reg);
    ShapeVariant vb = makeShapeVariant(eb, reg);
    return [va, vb](math::Vec3 dir) -> math::Vec3 {
        return std::visit([&dir](const auto& a, const auto& b) {
            return support(a, b, dir);
        }, va, vb);
    };
}

// Single source of truth for GJK's seed direction. gjkBoolean (oracle) and
// gjkTrace (stepper) both use this so they reproduce the same first iteration.
// NOTE: detectGJK currently computes its own seed inline — route it through this
// too (one-line change) if you want the stepper to mirror a seed experiment there.
static math::Vec3 gjkInitDir(const ShapeVariant& va, const ShapeVariant& vb) {
    math::Vec3 d = shapePosition(vb) - shapePosition(va);
    if (d.dot(d) < 1e-8f) d = {0.0f, -1.0f, 0.0f};  // coincident centers fallback
    return d;
}

bool gjkBoolean(ecs::Entity ea, ecs::Entity eb, ecs::Registry& reg, GJKSimplex* outSimplex) {
    auto* tfa = reg.get<Transform>(ea);
    auto* tfb = reg.get<Transform>(eb);
    if (!tfa || !tfb) return false;

    ShapeVariant va = makeShapeVariant(ea, reg);
    ShapeVariant vb = makeShapeVariant(eb, reg);
    auto supportFunction = makeSupport(va, vb);

    GJKSimplex simplex;
    math::Vec3 initDir = gjkInitDir(va, vb);

    bool hit = gjkIntersect(supportFunction, simplex, initDir);
    if (outSimplex) *outSimplex = simplex;
    return hit;
}

bool gjkTrace(ecs::Entity ea, ecs::Entity eb, ecs::Registry& reg, GJKTrace& outTrace) {
    outTrace.clear();
    auto* tfa = reg.get<Transform>(ea);
    auto* tfb = reg.get<Transform>(eb);
    if (!tfa || !tfb) return false;

    ShapeVariant va = makeShapeVariant(ea, reg);
    ShapeVariant vb = makeShapeVariant(eb, reg);
    auto supportFunction = makeSupport(va, vb);

    GJKSimplex simplex;
    math::Vec3 initDir = gjkInitDir(va, vb);
    // Runs the REAL templated gjkIntersect with recording on — the stepper then
    // scrubs outTrace, so it can never drift from the actual loop again.
    return gjkIntersect(supportFunction, simplex, initDir, &outTrace);
}

bool satBoolean(ecs::Entity ea, ecs::Entity eb, ecs::Registry& reg) {
    auto* tfa = reg.get<Transform>(ea);
    auto* tfb = reg.get<Transform>(eb);
    if (!tfa || !tfb) return false;

    auto* sa = reg.get<ecs::SphereForm>(ea); auto* sb = reg.get<ecs::SphereForm>(eb);
    auto* aa = reg.get<ecs::AABBForm>(ea);   auto* ab = reg.get<ecs::AABBForm>(eb);
    auto* ca = reg.get<ecs::OBBForm>(ea);    auto* cb = reg.get<ecs::OBBForm>(eb);

    auto mSph  = [&](ecs::Entity e, float r)        -> SphereGeom { return {reg.get<Transform>(e)->position, r};   };
    auto mAABB = [&](ecs::Entity e, math::Vec3 ext) -> AABBGeom   { return {reg.get<Transform>(e)->position, ext}; };
    auto mOBB  = [&](ecs::Entity e, math::Vec3 ext) -> OBBGeom    {
        auto* tf = reg.get<Transform>(e);
        return {tf->position, ext, math::Mat3::rotation(tf->rotation)};
    };

    ContactManifold m;
    if (sa && sb) return detectSphereSphere(mSph(ea,sa->radius), mSph(eb,sb->radius), m);
    if (sa && ab) return detectSphereAABB (mSph(ea,sa->radius), mAABB(eb,ab->extent), m);
    if (aa && sb) return detectSphereAABB (mSph(eb,sb->radius), mAABB(ea,aa->extent), m);
    if (aa && ab) return detectAABBAABB   (mAABB(ea,aa->extent), mAABB(eb,ab->extent), m);
    if (sa && cb) return detectSphereOBB  (mSph(ea,sa->radius), mOBB(eb,cb->extent), m);
    if (ca && sb) return detectSphereOBB  (mSph(eb,sb->radius), mOBB(ea,ca->extent), m);
    if (aa && cb) return detectAABBOBB    (mAABB(ea,aa->extent), mOBB(eb,cb->extent), m);
    if (ca && ab) return detectAABBOBB    (mAABB(eb,ab->extent), mOBB(ea,ca->extent), m);
    if (ca && cb) return detectOBBOBB     (mOBB(ea,ca->extent), mOBB(eb,cb->extent), m);
    return false;
}

} // namespace physics::ColliderDiscrete
