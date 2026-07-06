#include "ColliderDiscrete.h"
#include "PhysicsConstants.h"
#include "../ecs/Registry.h"
#include "../ecs/Components.h"
#include "../world/Transform.h"
#include "../debug/Profiler.h"
#include <cmath>
#include <algorithm>
#include <vector>

namespace physics::ColliderDiscrete {

// ============================================================================
// Per-shape-pair narrowphase timing (A4).
// Each detect() call falls into one of 6 buckets (sph_sph, sph_aabb, sph_obb,
// aabb_aabb, aabb_obb, obb_obb). NPHASE_TIME(bucket) creates a thread-local
// RAII timer in debug builds; in release it expands to nothing.
// emitNarrowphaseProfile() pushes 6 records (even at count==0) so the CSV
// has a stable 6-row footprint per step for easy pandas pivot.
// (PairBucket/kBucketStage/NPHASE_TIME live in ColliderTypes.h — shared with
// detectGJK() in ColliderGJK.cpp, which records into the same accumulator.)
// ============================================================================

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

// Maps a pair of ShapeVariant::index() values (normalized ai<=bi) to its
// timing bucket. 5×5 table: indices 0=Sphere, 1=AABB, 2=OBB, 3=Capsule, 4=Cylinder.
PairBucket getBucket(int ai, int bi) {
    if (ai > bi) std::swap(ai, bi);
    constexpr PairBucket table[5][5] = {
        { NP_SPH_SPH,   NP_SPH_AABB,  NP_SPH_OBB,   NP_GJK_OTHER, NP_GJK_OTHER },
        { NP_SPH_AABB,  NP_AABB_AABB, NP_AABB_OBB,  NP_GJK_OTHER, NP_GJK_OTHER },
        { NP_SPH_OBB,   NP_AABB_OBB,  NP_OBB_OBB,   NP_GJK_OTHER, NP_GJK_OTHER },
        { NP_GJK_OTHER, NP_GJK_OTHER, NP_GJK_OTHER,  NP_GJK_OTHER, NP_GJK_OTHER },
        { NP_GJK_OTHER, NP_GJK_OTHER, NP_GJK_OTHER,  NP_GJK_OTHER, NP_GJK_OTHER },
    };
    if (ai < 0 || ai >= 5 || bi < 0 || bi >= 5) return NP_GJK_OTHER;
    return table[ai][bi];
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

            c.lambda[i]   = it->second.normal * 0.999f;
            c.lambdaT1[i] = it->second.t1     * 0.995f;
            c.lambdaT2[i] = it->second.t2     * 0.995f;
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

    if (sa && sb) { NPHASE_TIME(NP_SPH_SPH);   if (analyticalSphereSphere(mSph(ea,sa->radius), mSph(eb,sb->radius), m))    push(ea,eb,m); return; }
    if (sa && ab) { NPHASE_TIME(NP_SPH_AABB);  if (analyticalSphereAABB(mSph(ea,sa->radius), mAABB(eb,ab->extent), m))   { m.normal=-m.normal; push(ea,eb,m); } return; }
    if (aa && sb) { NPHASE_TIME(NP_SPH_AABB);  if (analyticalSphereAABB(mSph(eb,sb->radius), mAABB(ea,aa->extent), m))     push(ea,eb,m); return; }
    if (aa && ab) { NPHASE_TIME(NP_AABB_AABB); if (analyticalAABBAABB(mAABB(ea,aa->extent), mAABB(eb,ab->extent), m))      push(ea,eb,m); return; }
    if (sa && cb) { NPHASE_TIME(NP_SPH_OBB);   if (analyticalSphereOBB(mSph(ea,sa->radius), mOBB(eb,cb->extent), m))    { m.normal=-m.normal; push(ea,eb,m); } return; }
    if (ca && sb) { NPHASE_TIME(NP_SPH_OBB);   if (analyticalSphereOBB(mSph(eb,sb->radius), mOBB(ea,ca->extent), m))      push(ea,eb,m); return; }
    if (aa && cb) { NPHASE_TIME(NP_AABB_OBB);  if (analyticalAABBOBB(mAABB(ea,aa->extent), mOBB(eb,cb->extent), m))       push(ea,eb,m); return; }
    if (ca && ab) { NPHASE_TIME(NP_AABB_OBB);  if (analyticalAABBOBB(mAABB(eb,ab->extent), mOBB(ea,ca->extent), m))     { m.normal=-m.normal; push(ea,eb,m); } return; }
    if (ca && cb) { NPHASE_TIME(NP_OBB_OBB);   if (analyticalOBBOBB(mOBB(ea,ca->extent), mOBB(eb,cb->extent), m))         push(ea,eb,m); return; }
}

} // namespace physics::ColliderDiscrete
