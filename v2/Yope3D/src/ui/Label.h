#pragma once
#include "gpu/UIBuffer.h"
#include <vector>

// ---------------------------------------------------------------------------
// Label — abstract base for all UI elements.
//
// Coordinate convention: all positions are in [0,1] percentage of screen
// with (0,0) at top-left and (1,1) at bottom-right.
// Conversion to NDC is: ndcX = x*2-1, ndcY = 1-y*2.
// ---------------------------------------------------------------------------

class Label {
public:
    virtual ~Label() = default;

    // Build geometry into buf for this frame. screenW/screenH in pixels.
    virtual void buildMesh(UIBuffer& buf, float screenW, float screenH) = 0;

    // Called each frame before buildMesh().
    virtual void update(float dt) {}

    // Called when window is resized.
    virtual void onResize(float newW, float newH) {}

    // Called when LMB is pressed. fx,fy in [0,1] screen coords.
    virtual void onClicked(float fx, float fy, int button, int action) {}

    // Default hit-test: true if (fx,fy) falls within [min,max].
    virtual bool hitTest(float fx, float fy) const { return false; }

    // Draw-order priority — higher depth renders on top.
    virtual int getDepth() const = 0;

    virtual bool isVisible() const { return true; }

    // Populated by buildMesh(); read by Renderer after buildMesh() returns.
    UIDrawCall drawCall{};

    // Draw calls beyond the first, for labels that can't be drawn in one go.
    // Only styled text needs this: each run of <b>/<i> resolves to a different
    // atlas, and an atlas is a descriptor set, so it must be its own draw call.
    // Everything else is a single quad batch and leaves this null.
    virtual const std::vector<UIDrawCall>* extraDrawCalls() const { return nullptr; }
};
