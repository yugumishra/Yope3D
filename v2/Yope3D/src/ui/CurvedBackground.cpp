#include "CurvedBackground.h"
#include <vector>
#include <cmath>
#include <numbers>

CurvedBackground::CurvedBackground(math::Vec2 min, math::Vec2 max, math::Vec4 color,
                                   int depth, float curvature)
    : Background(min, max, color, depth), curvature_(curvature) {}

// Build a rounded rectangle with all four corners curved.
// Corner radius in NDC = min(halfW, halfH) * curvature_, clamped to half of each dimension.
void CurvedBackground::buildMesh(UIBuffer& buf, float /*screenW*/, float /*screenH*/) {
    if (!visible_) { drawCall = {}; return; }

    float x0 = toNdcX(min_.x), y0 = toNdcY(min_.y);
    float x1 = toNdcX(max_.x), y1 = toNdcY(max_.y);
    float r = color_.x, g = color_.y, b = color_.z, a = color_.w;

    // Positive half-dimensions in NDC.
    float halfW = (x1 - x0) * 0.5f;
    float halfH = (y1 - y0) * 0.5f;

    // Corner radius: scale of the smaller half-dimension, clamped so it fits in both axes.
    float arcR = std::min(halfW, halfH) * curvature_;
    arcR = std::max(arcR, 0.0f);

    // Inner rectangle edges (after subtracting the corner radius).
    float ix0 = x0 + arcR, ix1 = x1 - arcR;
    float iy0 = y0 + arcR, iy1 = y1 - arcR;

    std::vector<UIVertex> verts;
    std::vector<uint32_t> indices;
    verts.reserve(8 + 4 * (kArcSegments + 1));
    indices.reserve(18 + 4 * kArcSegments * 3);

    auto addV = [&](float vx, float vy) {
        verts.push_back({ vx, vy, 0.f, 0.f, r, g, b, a });
    };
    auto vi = [&]() -> uint32_t { return static_cast<uint32_t>(verts.size()); };

    // Centre rect  (ix0, iy0) → (ix1, iy1)
    {
        uint32_t b0 = vi();
        addV(ix0, iy0); addV(ix1, iy0); addV(ix1, iy1); addV(ix0, iy1);
        indices.insert(indices.end(), {b0,b0+1,b0+2, b0,b0+2,b0+3});
    }
    // Top bar:   full width, y0 → iy0
    if (arcR > 1e-6f) {
        uint32_t b0 = vi();
        addV(ix0, y0); addV(ix1, y0); addV(ix1, iy0); addV(ix0, iy0);
        indices.insert(indices.end(), {b0,b0+1,b0+2, b0,b0+2,b0+3});
    }
    // Bottom bar: full width, iy1 → y1
    if (arcR > 1e-6f) {
        uint32_t b0 = vi();
        addV(ix0, iy1); addV(ix1, iy1); addV(ix1, y1); addV(ix0, y1);
        indices.insert(indices.end(), {b0,b0+1,b0+2, b0,b0+2,b0+3});
    }
    // Left bar:  x0 → ix0, iy0 → iy1
    if (arcR > 1e-6f) {
        uint32_t b0 = vi();
        addV(x0, iy0); addV(ix0, iy0); addV(ix0, iy1); addV(x0, iy1);
        indices.insert(indices.end(), {b0,b0+1,b0+2, b0,b0+2,b0+3});
    }
    // Right bar: ix1 → x1, iy0 → iy1
    if (arcR > 1e-6f) {
        uint32_t b0 = vi();
        addV(ix1, iy0); addV(x1, iy0); addV(x1, iy1); addV(ix1, iy1);
        indices.insert(indices.end(), {b0,b0+1,b0+2, b0,b0+2,b0+3});
    }

    // Four corner arc fans.  Each fan's center is at the inner-rect corner.
    // The arc sweeps through π/2 from one axis-aligned edge to the other.
    if (arcR > 1e-6f) {
        float pi = static_cast<float>(std::numbers::pi);
        struct Corner { float cx, cy, startAngle; };
        Corner corners[4] = {
            { ix0, iy0, pi },           // top-left:     π → 3π/2
            { ix1, iy0, pi * 1.5f },    // top-right:    3π/2 → 2π
            { ix1, iy1, 0.f },          // bottom-right: 0 → π/2
            { ix0, iy1, pi * 0.5f },    // bottom-left:  π/2 → π
        };

        for (auto& c : corners) {
            uint32_t center = vi();
            addV(c.cx, c.cy);
            for (int i = 0; i <= kArcSegments; ++i) {
                float t     = static_cast<float>(i) / kArcSegments;
                float theta = c.startAngle + (pi * 0.5f) * t;
                addV(c.cx + arcR * std::cos(theta),
                     c.cy + arcR * std::sin(theta));
                if (i > 0) {
                    indices.push_back(center);
                    indices.push_back(center + i);
                    indices.push_back(center + i + 1);
                }
            }
        }
    }

    auto range = buf.push(verts.data(), static_cast<uint32_t>(verts.size()),
                          indices.data(), static_cast<uint32_t>(indices.size()));
    drawCall = { range.indexCount, range.indexOffset, range.vertexOffset, 0, VK_NULL_HANDLE };
}
