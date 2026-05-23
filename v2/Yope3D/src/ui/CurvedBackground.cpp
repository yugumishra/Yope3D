#include "CurvedBackground.h"
#include <vector>
#include <cmath>
#include <numbers>

CurvedBackground::CurvedBackground(math::Vec2 min, math::Vec2 max, math::Vec4 color,
                                   int depth, float curvature)
    : Background(min, max, color, depth), curvature_(curvature) {}

void CurvedBackground::buildMesh(UIBuffer& buf, float screenW, float screenH) {
    if (!visible_) { drawCall = {}; return; }

    // The arc radius in NDC is proportional to panel half-width × curvature.
    // The arc bulges downward from the bottom edge of the panel.
    float x0 = toNdcX(min_.x), y0 = toNdcY(min_.y);
    float x1 = toNdcX(max_.x), y1 = toNdcY(max_.y);
    float r = color_.x, g = color_.y, b = color_.z, a = color_.w;

    float halfW  = (x1 - x0) * 0.5f;
    float cx     = (x0 + x1) * 0.5f;  // center X of arc
    float radius = halfW * curvature_; // arc radius in NDC (Y axis, downward)

    // Rectangular body (top portion of the panel):
    // From y0 (top) to y1 (bottom edge of rectangle, where arc starts).
    std::vector<UIVertex> verts;
    std::vector<uint32_t> indices;
    verts.reserve(4 + kArcSegments + 1);
    indices.reserve(6 + kArcSegments * 3);

    auto addV = [&](float vx, float vy, float vu, float vv) {
        verts.push_back({ vx, vy, vu, vv, r, g, b, a });
    };

    // 0=top-left, 1=top-right, 2=bottom-right, 3=bottom-left (rectangle body)
    addV(x0, y0, 0.0f, 0.0f);
    addV(x1, y0, 1.0f, 0.0f);
    addV(x1, y1, 1.0f, 1.0f);
    addV(x0, y1, 0.0f, 1.0f);
    indices.insert(indices.end(), { 0,1,2, 0,2,3 });

    // Arc fan centered at (cx, y1) bulging downward:
    // Vertex 4 = arc center
    addV(cx, y1, 0.5f, 1.0f);
    uint32_t centerIdx = 4;

    float pi = static_cast<float>(std::numbers::pi);
    for (int i = 0; i <= kArcSegments; ++i) {
        float t  = static_cast<float>(i) / kArcSegments;
        float angle = pi * t;                         // 0 → π (left to right)
        float ax = cx + radius * std::cos(pi - angle); // x on arc (NDC)
        float ay = y1 + radius * std::sin(angle);      // y on arc, going down

        // UV: map to bottom strip of texture
        float uv = t;
        addV(ax, ay, uv, 1.0f);

        if (i > 0) {
            uint32_t prev = centerIdx + i;
            uint32_t curr = centerIdx + i + 1;
            indices.push_back(centerIdx);
            indices.push_back(prev);
            indices.push_back(curr);
        }
    }

    auto range = buf.push(verts.data(), static_cast<uint32_t>(verts.size()),
                          indices.data(), static_cast<uint32_t>(indices.size()));
    drawCall = { range.indexCount, range.indexOffset, range.vertexOffset, 0, VK_NULL_HANDLE };
}
