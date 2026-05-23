#pragma once
#include "Background.h"

// ---------------------------------------------------------------------------
// CurvedBackground — Background with a rounded bottom edge.
// curvature is in [0,1]: 0 = flat (same as Background), 1 = full half-circle.
// The arc is generated with 16 subdivisions for a smooth appearance.
// ---------------------------------------------------------------------------

class CurvedBackground : public Background {
public:
    CurvedBackground(math::Vec2 min, math::Vec2 max, math::Vec4 color, int depth,
                     float curvature = 0.5f);
    ~CurvedBackground() override = default;

    void buildMesh(UIBuffer& buf, float screenW, float screenH) override;

    void setCurvature(float c) { curvature_ = c; }

private:
    float curvature_ = 0.5f;
    static constexpr int kArcSegments = 16;
};
