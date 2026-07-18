#pragma once
#ifdef YOPE_EDITOR
#include "editor/EditorPanel.h"
#include "ecs/Entity.h"
#include "physics/ColliderDiscrete.h"
#include "math/Vec3.h"
#include <functional>

// GJK simplex stepper — single-pair deep debugger.
//
// Runs the REAL gjkIntersect once (via ColliderDiscrete::gjkTrace) with frame
// recording on, then scrubs the recorded GJKTrace. Because it replays the real
// run rather than re-implementing the loop, it can never drift from the actual
// algorithm — every edit to gjkIntersect / updateSimplex / the seed shows up
// here automatically. You can also scrub backward.
//
// Visualizes, via the debug-line pipeline: the CSO (Minkowski difference A-B,
// sampled with the real support fn), the simplex at the current frame (vertices
// color-coded by age + edges), the origin marker, the search-direction ray, and
// the support point probed that frame.
class GJKStepperPanel : public EditorPanel {
public:
    const char* name() const override { return "GJK Stepper"; }
    void draw(EditorContext& ctx) override;

private:
    void capturePair(EditorContext& ctx);
    void runTrace(EditorContext& ctx);    // (re)run the real gjkIntersect with recording
    void buildLines(EditorContext& ctx);

    ecs::Entity a_{}, b_{};
    bool        hasPair_ = false;

    // Support fn for the CSO point cloud only (the stepping itself uses the trace).
    std::function<math::Vec3(math::Vec3)> support_;

    physics::ColliderDiscrete::GJKTrace trace_;
    int  frame_ = 0;       // index into trace_
    bool ran_   = false;
    bool hit_   = false;   // overall gjkIntersect verdict for the whole run

    // Viz toggles
    bool  showCSO_       = true;
    bool  showSimplex_   = true;
    bool  showOrigin_    = true;
    bool  showDir_       = true;
    bool  showSupport_   = true;
    bool  offsetToPair_  = false;
    int   csoSamples_    = 240;

    // Auto-play (advances the scrub frame)
    bool  autoPlay_      = false;
    float autoInterval_  = 0.35f;
    float autoTimer_     = 0.0f;
};
#endif // YOPE_EDITOR
