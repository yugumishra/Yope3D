#pragma once
#include <cmath>

// ---------------------------------------------------------------------------
// Standard easing curves, t in [0,1] -> eased t in [0,1] (endpoints fixed).
// Header-only: no state, just the curve math. World owns the tween instances
// that drive UITransform::opacity over time (see World::tweenUIOpacity).
// ---------------------------------------------------------------------------

namespace ui {

enum class Ease : int {
    Linear = 0, QuadIn, QuadOut, QuadInOut, CubicIn, CubicOut, CubicInOut
};

inline float applyEase(Ease ease, float t) {
    switch (ease) {
        case Ease::Linear:    return t;
        case Ease::QuadIn:    return t * t;
        case Ease::QuadOut:   return t * (2.0f - t);
        case Ease::QuadInOut: return (t < 0.5f) ? 2.0f * t * t
                                                 : -1.0f + (4.0f - 2.0f * t) * t;
        case Ease::CubicIn:   return t * t * t;
        case Ease::CubicOut:  { float u = t - 1.0f; return u * u * u + 1.0f; }
        case Ease::CubicInOut: {
            if (t < 0.5f) return 4.0f * t * t * t;
            float u = -2.0f * t + 2.0f;
            return 1.0f - (u * u * u) * 0.5f;
        }
    }
    return t;
}

} // namespace ui
