#pragma once
#include "Label.h"
#include "math/Vec2.h"
#include "math/Vec4.h"

// ---------------------------------------------------------------------------
// Background — solid-color rectangular UI panel.
// All coordinates in [0,1] screen percentage (top-left origin).
// ---------------------------------------------------------------------------

class Background : public Label {
public:
    Background(math::Vec2 min, math::Vec2 max, math::Vec4 color, int depth);
    ~Background() override = default;

    void buildMesh(UIBuffer& buf, float screenW, float screenH) override;

    bool hitTest(float fx, float fy) const override;
    int  getDepth() const override { return depth_; }
    bool isVisible() const override { return visible_; }

    void setColor(math::Vec4 color)    { color_   = color;   }
    void setVisible(bool v)            { visible_ = v;       }
    void setBounds(math::Vec2 min, math::Vec2 max) { min_ = min; max_ = max; }

    math::Vec2 getMin() const { return min_; }
    math::Vec2 getMax() const { return max_; }

protected:
    math::Vec2 min_;
    math::Vec2 max_;
    math::Vec4 color_;
    int        depth_;
    bool       visible_ = true;

    // Helper: convert [0,1] % coords to NDC.
    static float toNdcX(float x) { return x * 2.0f - 1.0f; }
    static float toNdcY(float y) { return y * 2.0f - 1.0f; }
};
