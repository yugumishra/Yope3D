#include "TexturedBackground.h"

TexturedBackground::TexturedBackground(math::Vec2 min, math::Vec2 max, math::Vec4 tint,
                                       int depth, Texture* tex)
    : Background(min, max, tint, depth), texture_(tex) {}

void TexturedBackground::buildMesh(UIBuffer& buf, float screenW, float screenH) {
    if (!visible_ || !texture_) {
        // Fall back to solid-color rendering if no texture.
        Background::buildMesh(buf, screenW, screenH);
        return;
    }

    float x0 = toNdcX(min_.x), y0 = toNdcY(min_.y);
    float x1 = toNdcX(max_.x), y1 = toNdcY(max_.y);
    float r = color_.x, g = color_.y, b = color_.z, a = color_.w;

    UIVertex verts[4] = {
        { x0, y0,  0,0,  r,g,b,a },
        { x1, y0,  1,0,  r,g,b,a },
        { x1, y1,  1,1,  r,g,b,a },
        { x0, y1,  0,1,  r,g,b,a },
    };
    uint32_t indices[6] = { 0,1,2, 0,2,3 };

    auto range = buf.push(verts, 4, indices, 6);
    drawCall = { range.indexCount, range.indexOffset, range.vertexOffset,
                 1, texture_->getDescriptorSet() };
}
