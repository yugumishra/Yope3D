#pragma once
#include "ColliderGeom.h"
#include "../ecs/Entity.h"
#include "../math/Mat3.h"
#include "../math/Vec3.h"
#include <chrono>
#include <variant>

namespace physics {

namespace ColliderDiscrete {
    struct ContactManifold {
        math::Vec3 normal;
        float      penetration  = 0.0f;
        float      depths[4]    = {};
        math::Vec3 contactPoints[4];
        int        numContacts  = 0;
    };

    // ECS: Entity-keyed contact — used by advance().
    struct ActiveContact {
        ecs::Entity a;
        ecs::Entity b;
        ContactManifold manifold;
        math::Vec3      T1, T2;
        float           mu = 0.0f, e = 0.0f;
        math::Mat3      IinvA, IinvB;
        math::Vec3      rA[4], rB[4];
        float           W[4]    = {};
        float           Wt1[4]  = {};
        float           Wt2[4]  = {};
        float           neta[4] = {};
        float           lambda[4]   = {};
        float           lambdaT1[4] = {};
        float           lambdaT2[4] = {};
        float           lambdaP[4]  = {};
    };

    // Non-virtual dispatch across the 5 supported primitive shapes.
    using ShapeVariant = std::variant<SphereGeom, AABBGeom, OBBGeom, CapsuleGeom, CylinderGeom>;

    // ------------------------------------------------------------------------
    // Per-shape-pair narrowphase timing (Phase E profiler — A4). Lives here
    // (rather than hidden in ColliderDiscrete.cpp) because both detect()
    // (ColliderDiscrete.cpp) and detectGJK() (ColliderGJK.cpp) record into the
    // same per-thread accumulator; `inline` gives one shared definition across
    // translation units instead of an anonymous-namespace copy per TU.
    // ------------------------------------------------------------------------
    enum PairBucket {
        NP_SPH_SPH   = 0,
        NP_SPH_AABB  = 1,
        NP_SPH_OBB   = 2,
        NP_AABB_AABB = 3,
        NP_AABB_OBB  = 4,
        NP_OBB_OBB   = 5,
        NP_GJK_OTHER = 6,  // capsule/cylinder pairs (GJK-only)
        NP_BUCKETS   = 7,
    };

    inline constexpr const char* kBucketStage[NP_BUCKETS] = {
        "nphase_sph_sph",
        "nphase_sph_aabb",
        "nphase_sph_obb",
        "nphase_aabb_aabb",
        "nphase_aabb_obb",
        "nphase_obb_obb",
        "nphase_gjk_other",
    };

#ifndef NDEBUG
    struct NarrowphaseTiming {
        double us[NP_BUCKETS] = {};
        int    n [NP_BUCKETS] = {};
    };

    inline thread_local NarrowphaseTiming g_npTiming;

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

  #define NPHASE_TIME(bucket) ::physics::ColliderDiscrete::PairTimer _np_timer_{bucket}
#else
  #define NPHASE_TIME(bucket) ((void)0)
#endif

    // Maps a pair of ShapeVariant::index() values to its timing bucket.
    PairBucket getBucket(int ai, int bi);

} // namespace ColliderDiscrete
} // namespace physics
