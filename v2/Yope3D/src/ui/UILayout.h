#pragma once
#include "ecs/Components.h"
#include "math/Vec2.h"

// ---------------------------------------------------------------------------
// Anchor / fixed-pixel sizing resolution for UITransform.
//
// anchor == Free (0) is the legacy behavior: minX/minY/maxX/maxY are used
// verbatim as a [0,1] screen-fraction rect — every existing scene renders
// identically. Any other anchor value repositions the rect relative to a
// screen corner/edge/center; SizeMode::Pixel additionally sizes it in real
// screen pixels instead of [0,1] fractions, so elements stop stretching at
// non-square aspect ratios (limitations.md #2.3).
//
// Shared by Renderer::buildECSUIGeometry (visuals) and UIInputRouter
// (hit-testing) so clicks always land where the element is actually drawn.
// ---------------------------------------------------------------------------

namespace ui {

enum class Anchor : int {
    Free = 0, TopLeft, TopRight, BottomLeft, BottomRight, Center,
    CenterTop, CenterBottom, CenterLeft, CenterRight
};
enum class SizeMode : int { Fraction = 0, Pixel };

inline void resolveUITransformRect(const ecs::UITransform& tf, float screenW, float screenH,
                                    math::Vec2& outMin, math::Vec2& outMax) {
    auto anchor = static_cast<Anchor>(tf.anchor);
    if (anchor == Anchor::Free || screenW <= 0.0f || screenH <= 0.0f) {
        outMin = {tf.minX, tf.minY};
        outMax = {tf.maxX, tf.maxY};
        return;
    }

    auto  sizeMode = static_cast<SizeMode>(tf.sizeMode);
    float w = (sizeMode == SizeMode::Pixel) ? tf.pixelWidth  / screenW : (tf.maxX - tf.minX);
    float h = (sizeMode == SizeMode::Pixel) ? tf.pixelHeight / screenH : (tf.maxY - tf.minY);
    float ox = tf.offsetXPx / screenW;
    float oy = tf.offsetYPx / screenH;

    float apX = 0.5f, apY = 0.5f;
    switch (anchor) {
        case Anchor::TopLeft:     apX = 0.0f; apY = 0.0f; break;
        case Anchor::TopRight:    apX = 1.0f; apY = 0.0f; break;
        case Anchor::BottomLeft:  apX = 0.0f; apY = 1.0f; break;
        case Anchor::BottomRight: apX = 1.0f; apY = 1.0f; break;
        case Anchor::Center:      apX = 0.5f; apY = 0.5f; break;
        case Anchor::CenterTop:   apX = 0.5f; apY = 0.0f; break;
        case Anchor::CenterBottom:apX = 0.5f; apY = 1.0f; break;
        case Anchor::CenterLeft:  apX = 0.0f; apY = 0.5f; break;
        case Anchor::CenterRight: apX = 1.0f; apY = 0.5f; break;
        default: break;
    }

    float mnX, mxX;
    if      (apX <= 0.0f) { mnX = apX + ox; mxX = mnX + w; }
    else if (apX >= 1.0f) { mxX = apX - ox; mnX = mxX - w; }
    else                  { mnX = apX - w * 0.5f + ox; mxX = mnX + w; }

    float mnY, mxY;
    if      (apY <= 0.0f) { mnY = apY + oy; mxY = mnY + h; }
    else if (apY >= 1.0f) { mxY = apY - oy; mnY = mxY - h; }
    else                  { mnY = apY - h * 0.5f + oy; mxY = mnY + h; }

    outMin = {mnX, mnY};
    outMax = {mxX, mxY};
}

} // namespace ui
