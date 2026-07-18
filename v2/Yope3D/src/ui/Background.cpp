#include "Background.h"

Background::Background(math::Vec2 min, math::Vec2 max, math::Vec4 color, int depth)
    : min_(min), max_(max), color_(color), depth_(depth) {}

bool Background::hitTest(float fx, float fy) const {
    return visible_ && fx >= min_.x && fx <= max_.x && fy >= min_.y && fy <= max_.y;
}

void Background::buildMesh(UIBuffer& buf, float /*screenW*/, float /*screenH*/) {
    if (!visible_) {
        drawCall = {};
        return;
    }

    float x0 = toNdcX(min_.x), y0 = toNdcY(min_.y);
    float x1 = toNdcX(max_.x), y1 = toNdcY(max_.y);
    float r = color_.x, g = color_.y, b = color_.z, a = color_.w;

    UIVertex verts[4] = {
        { x0, y0,  0,0,  r,g,b,a },   // top-left
        { x1, y0,  1,0,  r,g,b,a },   // top-right
        { x1, y1,  1,1,  r,g,b,a },   // bottom-right
        { x0, y1,  0,1,  r,g,b,a },   // bottom-left
    };
    uint32_t indices[6] = { 0,1,2, 0,2,3 };

    auto range = buf.push(verts, 4, indices, 6);
    drawCall = { range.indexCount, range.indexOffset, range.vertexOffset, 0, VK_NULL_HANDLE };
}
