#pragma once
#include "Label.h"
#include "TextAtlas.h"
#include <string>

class Background;

// ---------------------------------------------------------------------------
// TextBox — renders a string of text using a TextAtlas.
//
// Bounds come from parent Background; if parent==nullptr, the full screen is used.
// Text is positioned in screen-pixel space, then converted to NDC for GPU.
// displayPx: desired character height in pixels. If 0, uses the atlas's native size.
// ---------------------------------------------------------------------------

enum class Alignment { DEFAULT, CENTERED };

class TextBox : public Label {
public:
    TextBox(Background* parent, TextAtlas* atlas, const std::string& text,
            int depth, int displayPx = 0, Alignment align = Alignment::DEFAULT);
    ~TextBox() override = default;

    void buildMesh(UIBuffer& buf, float screenW, float screenH) override;

    int  getDepth()   const override { return depth_;   }
    bool isVisible()  const override { return visible_; }

    void setText(const std::string& t) { text_ = t; }
    void setColor(float r, float g, float b, float a = 1.0f) {
        cr_ = r; cg_ = g; cb_ = b; ca_ = a;
    }
    void setVisible(bool v) { visible_ = v; }

private:
    Background* parent_    = nullptr;
    TextAtlas*  atlas_     = nullptr;
    std::string text_;
    int         depth_     = 0;
    int         displayPx_ = 0;
    Alignment   align_     = Alignment::DEFAULT;
    bool        visible_   = true;
    float       cr_ = 1, cg_ = 1, cb_ = 1, ca_ = 1;  // text color (white default)

    // Layout is authored against a 1920×1080 reference. refScale = min(W/1920, H/1080)
    // drives ALL metrics — glyph size, padding, and wrap width — so em-units-per-line
    // is constant across resolutions and aspect ratios ≤ 16:9. Ultra-wide displays
    // (AR > 16:9) get more characters per line (expected: the box is physically wider).
    static constexpr float kReferenceWidth  = 1920.0f;
    static constexpr float kReferenceHeight = 1080.0f;
    static constexpr float kPaddingPx       = 6.0f;   // padding inside parent bounds (reference px)
    static constexpr int   kDefaultDisplayPx = 32;    // character height when displayPx_==0 (reference px)
};
