#pragma once
#include "Label.h"
#include "TextAtlas.h"
#include <string>
#include <vector>

class Background;
class UIManager;

// ---------------------------------------------------------------------------
// TextBox — renders a string of text using a TextAtlas.
//
// Bounds come from parent Background; if parent==nullptr, the full screen is used.
// Text is positioned in screen-pixel space, then converted to NDC for GPU.
// displayPx: desired character height in pixels. If 0, uses the atlas's native size.
//
// The text may carry inline style tags (<b>/<bold>, <i>/<italic>, nestable) —
// see ui/TextLayout.h. Styling needs a UIManager to resolve the bold/italic
// atlas variants; pass one to enable it. Without a UIManager the tags are still
// stripped (rendering the markup literally would be worse) but every run draws
// in the base atlas.
// ---------------------------------------------------------------------------

enum class Alignment { DEFAULT, CENTERED };

class TextBox : public Label {
public:
    TextBox(Background* parent, TextAtlas* atlas, const std::string& text,
            int depth, int displayPx = 0, Alignment align = Alignment::DEFAULT,
            UIManager* uiMgr = nullptr);
    ~TextBox() override = default;

    void buildMesh(UIBuffer& buf, float screenW, float screenH) override;

    const std::vector<UIDrawCall>* extraDrawCalls() const override { return &extraDrawCalls_; }

    int  getDepth()   const override { return depth_;   }
    bool isVisible()  const override { return visible_; }

    void setText(const std::string& t) { text_ = t; }
    void setColor(float r, float g, float b, float a = 1.0f) {
        cr_ = r; cg_ = g; cb_ = b; ca_ = a;
    }
    void setVisible(bool v) { visible_ = v; }

    // Natural (unwrapped) size of `text` at `displayPx` reference pixels, in
    // ACTUAL on-screen pixels for the given screenW/screenH — the same
    // refScale/metrics math buildMesh uses to place glyphs, just without a
    // bounding box to wrap against (breaks only on explicit '\n'). Used by
    // UIText.autoSize (Renderer::buildECSUIGeometry) to size a box to fit its
    // text instead of the caller guessing width/height by hand.
    // Passing uiMgr measures styled runs against their real atlases (bold glyphs
    // are wider than regular); without it, everything is measured in `atlas`.
    static void measureNatural(TextAtlas* atlas, const std::string& text, int displayPx,
                               float screenW, float screenH, float& outWidthPx, float& outHeightPx,
                               UIManager* uiMgr = nullptr);

private:
    Background* parent_    = nullptr;
    TextAtlas*  atlas_     = nullptr;
    UIManager*  uiMgr_     = nullptr;   // null → tags stripped but not styled
    std::string text_;
    int         depth_     = 0;
    int         displayPx_ = 0;
    Alignment   align_     = Alignment::DEFAULT;
    bool        visible_   = true;
    float       cr_ = 1, cg_ = 1, cb_ = 1, ca_ = 1;  // text color (white default)

    // Second and later style runs; the first lands in Label::drawCall so that
    // the common untagged case still emits exactly one draw call.
    std::vector<UIDrawCall> extraDrawCalls_;

    // Layout is authored against a 1920×1080 reference. refScale = min(W/1920, H/1080)
    // drives ALL metrics — glyph size, padding, and wrap width — so em-units-per-line
    // is constant across resolutions and aspect ratios ≤ 16:9. Ultra-wide displays
    // (AR > 16:9) get more characters per line (expected: the box is physically wider).
    static constexpr float kReferenceWidth  = 1920.0f;
    static constexpr float kReferenceHeight = 1080.0f;
    static constexpr float kPaddingPx       = 6.0f;   // padding inside parent bounds (reference px)
    static constexpr int   kDefaultDisplayPx = 32;    // character height when displayPx_==0 (reference px)
};
