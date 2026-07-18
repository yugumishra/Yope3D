#pragma once
#ifdef YOPE_EDITOR
#include "editor/EditorPanel.h"
#include "ecs/Entity.h"
#include <vector>
#include <string>

// In-house GJK correctness tester (edit mode).
//
// Runs the in-progress gjkIntersect() against the proven SAT detect* routines
// as a ground-truth oracle over every collidable pair, and paints the physics
// debug overlay by verdict:
//     red    = GJK / SAT DISAGREE  (a bug)
//     green  = both say intersecting
//     gray   = both say disjoint
// A "Raw GJK" color mode is also provided (literal "red when detectGJK true").
//
// Disagreeing pairs are listed; clicking one selects both entities so they can
// be inspected (and handed to the simplex stepper, built later).
class GJKTestPanel : public EditorPanel {
public:
    const char* name() const override { return "GJK Test"; }
    void draw(EditorContext& ctx) override;

private:
    enum class Verdict { AgreeDisjoint, AgreeHit, FalsePositive, FalseNegative };
    enum class ColorMode { Oracle, RawGJK };
    enum class Scope     { AllPairs, SelectedVsAll };

    struct PairResult {
        ecs::Entity a, b;
        Verdict     verdict;
        std::string label;
    };

    void runOracle(EditorContext& ctx);

    bool      live_       = false;
    ColorMode colorMode_  = ColorMode::Oracle;
    Scope     scope_      = Scope::AllPairs;

    // Last-run stats / disagreement list (rebuilt each runOracle()).
    int                     pairsTested_   = 0;
    int                     disagreements_ = 0;
    int                     agreeHits_     = 0;
    std::vector<PairResult> mismatches_;
};
#endif // YOPE_EDITOR
