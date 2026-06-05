#include "TextBox.h"
#include "Background.h"
#include <algorithm>
#include <vector>

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

    // Uniform scale driven by whichever axis is more constrained relative to the
    // 1920×1080 reference. Because this single factor scales glyph size, padding,
    // AND the effective wrap width (advances), em-units-per-line stays constant
    // across any resolution or aspect ratio ≤ 16:9 — text wraps identically in
    // the editor viewport and in the runtime window regardless of their sizes.
    // Ultra-wide ARs (>16:9) become height-constrained: text is the same height
    // but the box is physically wider, so more characters fit per line (correct).
    float refScale = std::min(screenW / kReferenceWidth, screenH / kReferenceHeight);

    float pad   = kPaddingPx * refScale;
    float areaW = bMaxX - bMinX - pad * 2.0f;
    float areaH = bMaxY - bMinY - pad * 2.0f;
    if (areaW <= 0.0f || areaH <= 0.0f) { drawCall = {}; return; }

    float basePx  = (displayPx_ > 0) ? static_cast<float>(displayPx_)
                                     : static_cast<float>(kDefaultDisplayPx);
    float targetPx = basePx * refScale;
    // Auto-fit: if the ascender would overflow the box vertically, scale down so
    // text stays visible rather than silently clipped (small boxes / extreme aspect).
    float asc_em = atlas_->ascender();
    if (asc_em > 0.0f && asc_em * targetPx > areaH)
        targetPx = areaH / asc_em;

    float lineH = atlas_->lineHeight() * targetPx;
    float asc   = asc_em * targetPx;

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
            float adv = g->advance * targetPx;
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
    float penY = originY + asc;   // penY tracks the text baseline (screen Y-down)

    std::vector<UIVertex> verts;
    std::vector<uint32_t> indices;
    verts.reserve(text_.size() * 4);
    indices.reserve(text_.size() * 6);

    // Convert screen-pixel positions to NDC.
    auto ndcX = [&](float x) { return x / screenW * 2.0f - 1.0f; };
    auto ndcY = [&](float y) { return y / screenH * 2.0f - 1.0f; };

    for (char c : text_) {
        if (c == '\n') { penX = originX; penY += lineH; continue; }

        const GlyphInfo* g = atlas_->glyph(c);
        if (!g) continue;

        float adv = g->advance * targetPx;

        // Word-wrap: if this glyph overflows the right edge, move to next line.
        if (penX + adv > bMaxX - pad) {
            penX  = originX;
            penY += lineH;
        }
        if (penY > bMaxY - pad) break;  // baseline past bottom of bounds

        if (g->hasQuad) {
            // planeBounds are em, Y-up, baseline at origin → screen is Y-down,
            // baseline at penY: a point planeT em above baseline sits higher
            // (smaller screen y).
            float xMin = penX + g->planeL * targetPx;
            float xMax = penX + g->planeR * targetPx;
            float yTop = penY - g->planeT * targetPx;   // pair with v0 (atlas top)
            float yBot = penY - g->planeB * targetPx;   // pair with v1 (atlas bottom)

            uint32_t base = static_cast<uint32_t>(verts.size());
            verts.push_back({ ndcX(xMin), ndcY(yTop),  g->u0, g->v0,  cr_, cg_, cb_, ca_ });
            verts.push_back({ ndcX(xMax), ndcY(yTop),  g->u1, g->v0,  cr_, cg_, cb_, ca_ });
            verts.push_back({ ndcX(xMax), ndcY(yBot),  g->u1, g->v1,  cr_, cg_, cb_, ca_ });
            verts.push_back({ ndcX(xMin), ndcY(yBot),  g->u0, g->v1,  cr_, cg_, cb_, ca_ });
            indices.insert(indices.end(), { base, base+1, base+2, base, base+2, base+3 });
        }

        penX += adv;
    }

    if (verts.empty()) { drawCall = {}; return; }

    auto range = buf.push(verts.data(), static_cast<uint32_t>(verts.size()),
                          indices.data(), static_cast<uint32_t>(indices.size()));
    drawCall = { range.indexCount, range.indexOffset, range.vertexOffset,
                 2, atlas_->descriptorSet(), atlas_->distanceRange() };
}
