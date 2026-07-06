#pragma once
#include "ColliderSupports.h"
#include "PhysicsConstants.h"
#include <vector>
#include <functional>

namespace ecs { class Registry; }

namespace physics {

namespace ColliderDiscrete {

    //struct returned by gjk that is the simplex it generates (used by epa)
    struct GJKSimplex {
        math::Vec3 points[4];
        int n = 0;
    };

    // One recorded iteration of gjkIntersect, for the editor stepper. Purely
    // observational — gjkIntersect only writes these when a trace pointer is passed,
    // so the stepper replays the REAL run instead of re-implementing the loop.
    struct GJKTraceFrame {
        math::Vec3 dir;        // search direction used this iteration (post guard)
        math::Vec3 support;    // support point returned for that direction
        float      dotSD;      // support·dir (the termination-test value)
        int        simplexN;   // simplex size after updateSimplex this iteration
        math::Vec3 pts[4];     // simplex vertices after updateSimplex
        bool       early;      // updateSimplex reported origin contained
        bool       terminated; // this frame is the "no intersection" early exit
    };
    using GJKTrace = std::vector<GJKTraceFrame>;

    // Both the simplex and the search direction need to be updated so pass those
    // both in by reference. Declared here (defined in ColliderGJK.cpp) — must
    // precede gjkIntersect's definition below since it's a non-dependent call
    // inside that template and needs to be visible at template-definition time.
    bool updateSimplex(GJKSimplex& simplex, math::Vec3& direction);

    // Zero-cost: templated GJK takes support by template param, inlined completely.
    // Optional trace: when non-null, each iteration appends a GJKTraceFrame.
    template<typename SupportFn>
    bool gjkIntersect(SupportFn&& support, GJKSimplex& simplex, math::Vec3 initDir,
                      GJKTrace* trace = nullptr) {
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

    // EPA — stubbed (returns false). Templated like gjkIntersect, so it must stay
    // fully defined in this header rather than a .cpp.
    template<typename SupportFn>
    bool epaManifold(SupportFn&& support, GJKSimplex& simplex, ContactManifold& m) {
        return false;
    }

    //gjk detect
    void detectGJK(ecs::Entity ea, ecs::Entity eb, ecs::Registry& reg, std::vector<ActiveContact>& contacts);

    // ------------------------------------------------------------------------
    // GJK debug / test harness (editor oracle + simplex stepper).
    // Additive plumbing ONLY — none of this implements GJK; it just exposes the
    // existing pieces so editor-side tooling can drive the real algorithm.
    //   * makeSupportFn       — type-erased wrapper over makeSupport() (which returns
    //                          `auto`, hence uncallable across TUs); captures the two
    //                          shapes by value so the closure can outlive the call.
    //   * gjkBoolean          — runs the real templated gjkIntersect() on a pair and
    //                          reports intersection (+ optional final simplex).
    // ------------------------------------------------------------------------
    std::function<math::Vec3(math::Vec3)> makeSupportFn(ecs::Entity ea, ecs::Entity eb, ecs::Registry& reg);
    bool gjkBoolean(ecs::Entity ea, ecs::Entity eb, ecs::Registry& reg, GJKSimplex* outSimplex = nullptr);
    // Runs the real gjkIntersect on a pair with tracing on; fills outTrace and
    // returns the intersection verdict. The stepper scrubs outTrace's frames.
    bool gjkTrace(ecs::Entity ea, ecs::Entity eb, ecs::Registry& reg, GJKTrace& outTrace);

} // namespace ColliderDiscrete
} // namespace physics
