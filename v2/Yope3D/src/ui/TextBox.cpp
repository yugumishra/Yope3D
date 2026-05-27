#include "TextBox.h"
#include "Background.h"
#include <vector>
#include <cstring>

TextBox::TextBox(Background* parent, TextAtlas* atlas, const std::string& text,
                 int depth, int displayPx, Alignment align)
    : parent_(parent), atlas_(atlas), text_(text),
      depth_(depth), displayPx_(displayPx), align_(align) {}

void TextBox::buildMesh(UIBuffer& buf, float screenW, float screenH) {
    if (!visible_ || !atlas_ || text_.empty()) { drawCall = {}; return; }

    // Determine bounds in screen pixels.
    float bMinX = parent_ ? parent_->getMin().x * screenW : 0.0f;
    float bMinY = parent_ ? parent_->getMin().y * screenH : 0.0f;
    float bMaxX = parent_ ? parent_->getMax().x * screenW : screenW;
    float bMaxY = parent_ ? parent_->getMax().y * screenH : screenH;

    float pad   = kPaddingPx;
    float areaW = bMaxX - bMinX - pad * 2.0f;
    float areaH = bMaxY - bMinY - pad * 2.0f;
    if (areaW <= 0.0f || areaH <= 0.0f) { drawCall = {}; return; }

    int   natSize = atlas_->pixelSize();
    float scale   = (displayPx_ > 0) ? static_cast<float>(displayPx_) / natSize : 1.0f;
    // Auto-fit: if the ascender would overflow areaH, scale down so text is
    // visible rather than silently clipped (happens on low-DPI / small windows).
    if (atlas_->ascender() > 0 && atlas_->ascender() * scale > areaH)
        scale = areaH / static_cast<float>(atlas_->ascender());
    float lineH   = atlas_->lineHeight() * scale;
    float asc     = atlas_->ascender()   * scale;

    // ---------------------------------------------------------------------------
    // First pass (CENTERED only): measure total text extents.
    // ---------------------------------------------------------------------------
    float totalW = 0.0f, totalH = lineH;
    if (align_ == Alignment::CENTERED) {
        float rowW = 0.0f;
        for (char c : text_) {
            if (c == '\n') { totalW = std::max(totalW, rowW); rowW = 0; totalH += lineH; continue; }
            const GlyphInfo* g = atlas_->glyph(c);
            if (!g) continue;
            float adv = g->advance * scale;
            if (rowW + adv > areaW) { totalW = std::max(totalW, rowW); rowW = 0; totalH += lineH; }
            rowW += adv;
        }
        totalW = std::max(totalW, rowW);
    }

    // ---------------------------------------------------------------------------
    // Second pass: generate quads.
    // ---------------------------------------------------------------------------
    float originX = bMinX + pad;
    float originY = bMinY + pad;

    if (align_ == Alignment::CENTERED) {
        originX += (areaW - totalW) * 0.5f;
        originY += (areaH - totalH) * 0.5f;
    }

    float penX = originX;
    float penY = originY + asc;

    float atlasF = static_cast<float>(TextAtlas::kAtlasSize);
    std::vector<UIVertex> verts;
    std::vector<uint32_t> indices;
    verts.reserve(text_.size() * 4);
    indices.reserve(text_.size() * 6);

    for (char c : text_) {
        if (c == '\n') { penX = originX; penY += lineH; continue; }
        if (c == ' ') {
            const GlyphInfo* sp = atlas_->glyph(' ');
            penX += sp ? sp->advance * scale : natSize * 0.25f * scale;
            continue;
        }

        const GlyphInfo* g = atlas_->glyph(c);
        if (!g) continue;

        float adv = g->advance * scale;
        float gW  = g->width   * scale;
        float gH  = g->rows    * scale;

        // Word-wrap: if this glyph overflows, move to next line.
        if (penX + adv > bMaxX - pad) {
            penX  = originX;
            penY += lineH;
        }
        if (penY > bMaxY - pad) break;  // past bottom of bounds

        float xMin = penX + g->bearingX * scale;
        float xMax = xMin + gW;
        float yMin = penY - g->bearingY * scale;
        float yMax = yMin + gH;

        // Convert screen-pixel positions to NDC.
        auto ndcX = [&](float x) { return x / screenW * 2.0f - 1.0f; };
        auto ndcY = [&](float y) { return y / screenH * 2.0f - 1.0f; };

        float u0 = g->atlasX / atlasF;
        float u1 = (g->atlasX + g->width) / atlasF;
        float v0 = g->atlasY / atlasF;
        float v1 = (g->atlasY + g->rows)  / atlasF;

        uint32_t base = static_cast<uint32_t>(verts.size());
        verts.push_back({ ndcX(xMin), ndcY(yMin),  u0, v0,  cr_, cg_, cb_, ca_ });
        verts.push_back({ ndcX(xMax), ndcY(yMin),  u1, v0,  cr_, cg_, cb_, ca_ });
        verts.push_back({ ndcX(xMax), ndcY(yMax),  u1, v1,  cr_, cg_, cb_, ca_ });
        verts.push_back({ ndcX(xMin), ndcY(yMax),  u0, v1,  cr_, cg_, cb_, ca_ });
        indices.insert(indices.end(), { base, base+1, base+2, base, base+2, base+3 });

        penX += adv;
    }

    if (verts.empty()) { drawCall = {}; return; }

    auto range = buf.push(verts.data(), static_cast<uint32_t>(verts.size()),
                          indices.data(), static_cast<uint32_t>(indices.size()));
    drawCall = { range.indexCount, range.indexOffset, range.vertexOffset,
                 2, atlas_->descriptorSet() };
}
