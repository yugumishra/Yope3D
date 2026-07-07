#include "ColliderDiscrete.h"
#include "PhysicsConstants.h"
#include "CompoundShape.h"
#include "../ecs/Registry.h"
#include "../ecs/Components.h"
#include "../world/Transform.h"
#include "../debug/Profiler.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <optional>
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
            auto it = cache.find({c.a, c.b, c.shapeKey * 4 + i});
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
            cache[{c.a, c.b, c.shapeKey * 4 + i}] = {c.lambda[i], c.lambdaT1[i], c.lambdaT2[i]};
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
// Geometry-only pair dispatch (shared by detect() semantics and detectCompound).
// Produces a manifold whose normal points from A toward B. Covers the same
// Sphere/AABB/OBB pairs detect() handles analytically; Capsule/Cylinder (and any
// pair without a closed-form routine) return false — matching detect()'s own
// coverage until GJK/EPA is wired in.
// ============================================================================
static bool detectGeomPair(const ShapeVariant& A, const ShapeVariant& B, ContactManifold& m) {
    auto* sa = std::get_if<SphereGeom>(&A); auto* aa = std::get_if<AABBGeom>(&A); auto* oa = std::get_if<OBBGeom>(&A);
    auto* sb = std::get_if<SphereGeom>(&B); auto* ab = std::get_if<AABBGeom>(&B); auto* ob = std::get_if<OBBGeom>(&B);

    // Each branch normalizes the analytical routine's native normal to A->B.
    // Routine conventions: SphereAABB/SphereOBB emit box->sphere; AABBOBB emits
    // aabb->obb; the symmetric routines emit first-arg->second-arg.
    if (sa && sb) return analyticalSphereSphere(*sa, *sb, m);
    if (sa && ab) { if (analyticalSphereAABB(*sa, *ab, m)) { m.normal = -m.normal; return true; } return false; }
    if (sa && ob) { if (analyticalSphereOBB (*sa, *ob, m)) { m.normal = -m.normal; return true; } return false; }
    if (aa && sb) return analyticalSphereAABB(*sb, *aa, m);
    if (aa && ab) return analyticalAABBAABB  (*aa, *ab, m);
    if (aa && ob) return analyticalAABBOBB   (*aa, *ob, m);
    if (oa && sb) return analyticalSphereOBB (*sb, *oa, m);
    if (oa && ab) { if (analyticalAABBOBB    (*ab, *oa, m)) { m.normal = -m.normal; return true; } return false; }
    if (oa && ob) return analyticalOBBOBB    (*oa, *ob, m);
    return false;
}

// Build a world-space ShapeVariant for an entity's single collider Form.
// Returns nullopt for shapes without an analytical narrowphase (capsule/cylinder)
// or entities with no recognized form.
static std::optional<ShapeVariant> buildEntityWorldGeom(ecs::Entity e, ecs::Registry& reg) {
    auto* tf = reg.get<Transform>(e);
    if (!tf) return std::nullopt;
    if (auto* sf = reg.get<ecs::SphereForm>(e)) return ShapeVariant{SphereGeom{tf->position, sf->radius}};
    if (auto* af = reg.get<ecs::AABBForm>(e))   return ShapeVariant{AABBGeom{tf->position, af->extent}};
    if (auto* of = reg.get<ecs::OBBForm>(e))
        return ShapeVariant{OBBGeom{tf->position, of->extent, math::Mat3::rotation(tf->rotation)}};
    return std::nullopt;
}

// Conservative world-space AABB of a Sphere/AABB/OBB geom (OBB rotation-fattened).
static void worldAABBofGeom(const ShapeVariant& g, math::Vec3& mn, math::Vec3& mx) {
    if (auto* s = std::get_if<SphereGeom>(&g)) {
        math::Vec3 r{s->radius, s->radius, s->radius};
        mn = s->pos - r; mx = s->pos + r;
    } else if (auto* a = std::get_if<AABBGeom>(&g)) {
        mn = a->pos - a->extent; mx = a->pos + a->extent;
    } else if (auto* o = std::get_if<OBBGeom>(&g)) {
        const math::Mat3& R = o->rot; const math::Vec3& x = o->extent;
        math::Vec3 f{
            std::fabs(R.m[0]) * x.x + std::fabs(R.m[3]) * x.y + std::fabs(R.m[6]) * x.z,
            std::fabs(R.m[1]) * x.x + std::fabs(R.m[4]) * x.y + std::fabs(R.m[7]) * x.z,
            std::fabs(R.m[2]) * x.x + std::fabs(R.m[5]) * x.y + std::fabs(R.m[8]) * x.z,
        };
        mn = o->pos - f; mx = o->pos + f;
    }
}

// Builds the world-space ShapeVariant for sub-shape `i` of a compound whose
// body transform is (Rbody, posBody). Returns nullopt for capsule/cylinder
// sub-shapes (no analytical pair yet).
static std::optional<ShapeVariant> buildSubShapeWorldGeom(const physics::CompiledCollider& col, int i,
                                                          const math::Mat3& Rbody, const math::Vec3& posBody) {
    const physics::SubShape& s = col.subShapes[i];
    math::Vec3 wpos = posBody + Rbody * s.localPos;
    switch (s.type) {
        case physics::SubShapeType::Sphere:
            return ShapeVariant{SphereGeom{wpos, s.extent.x}};
        case physics::SubShapeType::AABB:
        case physics::SubShapeType::OBB:
            return ShapeVariant{OBBGeom{wpos, s.extent, Rbody * s.localRot}};
        default:
            return std::nullopt;   // capsule/cylinder sub-shapes: no analytical pair yet
    }
}

// Descends `col`'s baked BVH (body-local frame defined by Rbody/posBody)
// against a world-space query AABB [wmn,wmx], invoking `fn(subIndex, subGeomWorld)`
// for every surviving leaf sub-shape with an analytical world geom. Shared by
// the compound-vs-single-shape and compound-vs-compound narrowphase paths.
template <class Fn>
static void descendCompoundBvh(const physics::CompiledCollider& col,
                               const math::Mat3& Rbody, const math::Vec3& posBody,
                               const math::Vec3& wmn, const math::Vec3& wmx, Fn&& fn) {
    // Query AABB (world) -> compound-local frame via the 8 corners.
    const math::Mat3 RbodyT = Rbody.transpose();
    constexpr float kInf = std::numeric_limits<float>::max();
    math::Vec3 qmn{kInf, kInf, kInf}, qmx{-kInf, -kInf, -kInf};
    for (int c = 0; c < 8; ++c) {
        math::Vec3 corner{ (c & 1) ? wmx.x : wmn.x, (c & 2) ? wmx.y : wmn.y, (c & 4) ? wmx.z : wmn.z };
        math::Vec3 local = RbodyT * (corner - posBody);
        qmn.x = std::min(qmn.x, local.x); qmn.y = std::min(qmn.y, local.y); qmn.z = std::min(qmn.z, local.z);
        qmx.x = std::max(qmx.x, local.x); qmx.y = std::max(qmx.y, local.y); qmx.z = std::max(qmx.z, local.z);
    }
    auto overlaps = [&](const BvhNode& n) {
        return !(qmn.x > n.aabbMax.x || qmx.x < n.aabbMin.x ||
                 qmn.y > n.aabbMax.y || qmx.y < n.aabbMin.y ||
                 qmn.z > n.aabbMax.z || qmx.z < n.aabbMin.z);
    };

    int32_t stack[64];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        const BvhNode& n = col.nodes[stack[--sp]];
        if (!overlaps(n)) continue;
        if (n.count > 0) {
            for (int i = n.first; i < n.first + n.count; ++i) {
                auto subGeom = buildSubShapeWorldGeom(col, i, Rbody, posBody);
                if (subGeom) fn(i, *subGeom);
            }
        } else {
            if (n.right >= 0 && sp < 64) stack[sp++] = n.right;
            if (n.left  >= 0 && sp < 64) stack[sp++] = n.left;
        }
    }
}

// Conservative world-space AABB of an entire compound body (root BVH bounds,
// rotation-fattened — same trick as worldAABBofGeom's OBB branch).
static void worldAABBofCompound(const physics::CompiledCollider& col,
                                const math::Mat3& Rbody, const math::Vec3& posBody,
                                math::Vec3& wmn, math::Vec3& wmx) {
    math::Vec3 center = (col.localMin + col.localMax) * 0.5f;
    math::Vec3 half   = (col.localMax - col.localMin) * 0.5f;
    math::Vec3 fat{
        std::fabs(Rbody.m[0]) * half.x + std::fabs(Rbody.m[3]) * half.y + std::fabs(Rbody.m[6]) * half.z,
        std::fabs(Rbody.m[1]) * half.x + std::fabs(Rbody.m[4]) * half.y + std::fabs(Rbody.m[7]) * half.z,
        std::fabs(Rbody.m[2]) * half.x + std::fabs(Rbody.m[5]) * half.y + std::fabs(Rbody.m[8]) * half.z,
    };
    math::Vec3 worldCenter = posBody + Rbody * center;
    wmn = worldCenter - fat;
    wmx = worldCenter + fat;
}

// ============================================================================
// detectCompound() — narrowphase for a compound body vs a single-shape body.
// Descends the compound's baked BVH with the other body's query AABB (in
// compound-local space), then runs the analytical pair test per surviving
// sub-shape. Pushes one manifold per colliding sub-shape, tagged with the
// sub-shape index as shapeKey so warm-start caching stays per-sub-shape.
// Contacts are always pushed as (a = compound, b = other); normal points
// compound -> other (the a->b convention solveIsland expects).
// NOTE: assumes the compound body's Transform has unit scale (levels bake scale
// into sub-shapes); body rotation/translation are honored.
// ============================================================================
static void detectCompound(ecs::Entity compoundEnt, ecs::Entity other,
                           ecs::Registry& reg, std::vector<ActiveContact>& contacts)
{
    auto* cc = reg.get<ecs::CompoundCollider>(compoundEnt);
    if (!cc || !cc->compiled || cc->compiled->nodes.empty()) return;
    const physics::CompiledCollider& col = *cc->compiled;

    auto* tfBody = reg.get<Transform>(compoundEnt);
    if (!tfBody) return;
    const math::Mat3 Rbody  = math::Mat3::rotation(tfBody->rotation);
    const math::Vec3 posBody = tfBody->position;

    auto otherGeomOpt = buildEntityWorldGeom(other, reg);
    if (!otherGeomOpt) return;
    const ShapeVariant& otherGeom = *otherGeomOpt;

    math::Vec3 wmn, wmx;
    worldAABBofGeom(otherGeom, wmn, wmx);

    descendCompoundBvh(col, Rbody, posBody, wmn, wmx, [&](int i, const ShapeVariant& subGeom) {
        ContactManifold m;
        if (detectGeomPair(subGeom, otherGeom, m)) {
            ActiveContact ac; ac.a = compoundEnt; ac.b = other; ac.shapeKey = i; ac.manifold = m;
            contacts.push_back(ac);
        }
    });
}

// ============================================================================
// detectCompoundCompound() — narrowphase for compound-vs-compound (e.g. a
// dynamic multi-part prop landing on a static compound level). For each
// sub-shape of A, cheap-reject its world AABB against B's overall world AABB,
// then descend B's BVH with that sub-shape's AABB (same machinery as
// detectCompound, just invoked once per A sub-shape instead of once for a
// single external shape). Contacts are pushed as (a, b) with normal a -> b;
// shapeKey combines both sub-shape indices (subA * subCountB + subB) so
// warm-start caching stays per-sub-shape-pair.
// ============================================================================
static void detectCompoundCompound(ecs::Entity a, ecs::Entity b,
                                   ecs::Registry& reg, std::vector<ActiveContact>& contacts)
{
    auto* ccA = reg.get<ecs::CompoundCollider>(a);
    auto* ccB = reg.get<ecs::CompoundCollider>(b);
    if (!ccA || !ccA->compiled || ccA->compiled->nodes.empty()) return;
    if (!ccB || !ccB->compiled || ccB->compiled->nodes.empty()) return;
    const physics::CompiledCollider& colA = *ccA->compiled;
    const physics::CompiledCollider& colB = *ccB->compiled;

    auto* tfA = reg.get<Transform>(a);
    auto* tfB = reg.get<Transform>(b);
    if (!tfA || !tfB) return;
    const math::Mat3 RbodyA = math::Mat3::rotation(tfA->rotation);
    const math::Vec3 posA   = tfA->position;
    const math::Mat3 RbodyB = math::Mat3::rotation(tfB->rotation);
    const math::Vec3 posB   = tfB->position;

    math::Vec3 wBmn, wBmx;
    worldAABBofCompound(colB, RbodyB, posB, wBmn, wBmx);
    auto overlapsB = [&](const math::Vec3& mn, const math::Vec3& mx) {
        return !(mn.x > wBmx.x || mx.x < wBmn.x ||
                 mn.y > wBmx.y || mx.y < wBmn.y ||
                 mn.z > wBmx.z || mx.z < wBmn.z);
    };

    const int subCountB = static_cast<int>(colB.subShapes.size());
    for (int iA = 0; iA < static_cast<int>(colA.subShapes.size()); ++iA) {
        auto subAGeomOpt = buildSubShapeWorldGeom(colA, iA, RbodyA, posA);
        if (!subAGeomOpt) continue;
        const ShapeVariant& subAGeom = *subAGeomOpt;

        math::Vec3 amn, amx;
        worldAABBofGeom(subAGeom, amn, amx);
        if (!overlapsB(amn, amx)) continue;

        descendCompoundBvh(colB, RbodyB, posB, amn, amx, [&](int iB, const ShapeVariant& subBGeom) {
            ContactManifold m;
            if (detectGeomPair(subAGeom, subBGeom, m)) {
                ActiveContact ac; ac.a = a; ac.b = b;
                ac.shapeKey = iA * subCountB + iB;
                ac.manifold = m;
                contacts.push_back(ac);
            }
        });
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

    // Compound colliders (baked multi-shape bodies) take a separate BVH-driven path.
    bool aCompound = reg.has<ecs::CompoundCollider>(ea);
    bool bCompound = reg.has<ecs::CompoundCollider>(eb);
    if (aCompound && bCompound)  { detectCompoundCompound(ea, eb, reg, contacts); return; }
    if (aCompound && !bCompound) { detectCompound(ea, eb, reg, contacts); return; }
    if (bCompound && !aCompound) { detectCompound(eb, ea, reg, contacts); return; }

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
