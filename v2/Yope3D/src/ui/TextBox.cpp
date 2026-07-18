#include "TextBox.h"
#include "Background.h"
#include "TextLayout.h"
#include "TextShaping.h"
#include <algorithm>
#include <vector>

namespace {

// Yields each glyph of `tok` already resolved to the atlas its style calls for.
// Without a UIManager there is nothing to resolve variants against, so every
// run falls back to `base` unstyled — the tags are still gone from the text,
// which is the point (rendering "<b>" literally would be the worse failure).
void walkGlyphs(UIManager* uiMgr, TextAtlas* base, const text::TokenizedText& tok,
                const text::GlyphVisitor& fn) {
    if (uiMgr) {
        text::shapeTokenized(*uiMgr, base->fontPath(), tok, fn);
        return;
    }
    size_t i = 0;
    while (i < tok.plain.size()) {
        text::ShapedGlyph sg{};
        sg.codepoint = text::decodeUtf8(tok.plain, i);
        if (sg.codepoint == U'\n') {
            sg.isNewline = true;
            if (!fn(sg)) return;
            continue;
        }
        sg.atlas = base;
        sg.glyph = base->glyph(sg.codepoint);
        if (!fn(sg)) return;
    }
}

} // namespace

void TextBox::measureNatural(TextAtlas* atlas, const std::string& text, int displayPx,
                             float screenW, float screenH, float& outWidthPx, float& outHeightPx,
                             UIManager* uiMgr) {
    outWidthPx = 0.0f;
    outHeightPx = 0.0f;
    if (!atlas || text.empty() || screenW <= 0.0f || screenH <= 0.0f) return;

    // Same reference-resolution scale factor buildMesh uses, so the measured
    // size matches what actually gets drawn at this screen size.
    float refScale = std::min(screenW / kReferenceWidth, screenH / kReferenceHeight);
    float basePx   = (displayPx > 0) ? static_cast<float>(displayPx)
                                     : static_cast<float>(kDefaultDisplayPx);
    float targetPx = basePx * refScale;
    // Vertical metrics always come from the base atlas: bold/italic faces differ
    // by a hair, and switching line height mid-paragraph would make lines jitter.
    float lineH    = atlas->lineHeight() * targetPx;

    const text::TokenizedText tok = text::tokenizeStyledText(text);

    float totalW = 0.0f, rowW = 0.0f;
    int   lines  = 1;
    walkGlyphs(uiMgr, atlas, tok, [&](const text::ShapedGlyph& sg) {
        if (sg.isNewline) { totalW = std::max(totalW, rowW); rowW = 0.0f; ++lines; return true; }
        if (!sg.glyph) return true;
        rowW += sg.glyph->advance * targetPx;
        return true;
    });
    totalW = std::max(totalW, rowW);

    outWidthPx  = totalW + kPaddingPx * refScale * 2.0f;
    outHeightPx = static_cast<float>(lines) * lineH + kPaddingPx * refScale * 2.0f;
}

TextBox::TextBox(Background* parent, TextAtlas* atlas, const std::string& text,
                 int depth, int displayPx, Alignment align, UIManager* uiMgr)
    : parent_(parent), atlas_(atlas), uiMgr_(uiMgr), text_(text),
      depth_(depth), displayPx_(displayPx), align_(align) {}

void TextBox::buildMesh(UIBuffer& buf, float screenW, float screenH) {
    // Every early-out below must leave BOTH outputs empty: a stale extra draw
    // call would otherwise be drained again against this frame's UIBuffer.
    extraDrawCalls_.clear();
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

    // Line metrics come from the base atlas even for styled runs — see measureNatural.
    float lineH = atlas_->lineHeight() * targetPx;
    float asc   = asc_em * targetPx;

    // Tokenized once and walked twice (measure, then emit): re-running the tag
    // scanner per pass would be pure waste.
    const text::TokenizedText tok = text::tokenizeStyledText(text_);

    // ---------------------------------------------------------------------------
    // First pass (CENTERED only): measure total text extents.
    // ---------------------------------------------------------------------------
    float totalW = 0.0f, totalH = lineH;
    if (align_ == Alignment::CENTERED) {
        float rowW = 0.0f;
        walkGlyphs(uiMgr_, atlas_, tok, [&](const text::ShapedGlyph& sg) {
            if (sg.isNewline) { totalW = std::max(totalW, rowW); rowW = 0; totalH += lineH; return true; }
            if (!sg.glyph) return true;
            float adv = sg.glyph->advance * targetPx;
            if (rowW + adv > areaW) { totalW = std::max(totalW, rowW); rowW = 0; totalH += lineH; }
            rowW += adv;
            return true;
        });
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

    // An atlas is a descriptor set and boldBias is a push constant, so both are
    // fixed for the life of a draw call — a change in either ends the batch.
    TextAtlas* batchAtlas = nullptr;
    float      batchBias  = 0.0f;
    int        emitted    = 0;

    auto flush = [&]() {
        if (verts.empty() || !batchAtlas) return;
        auto range = buf.push(verts.data(), static_cast<uint32_t>(verts.size()),
                              indices.data(), static_cast<uint32_t>(indices.size()));
        UIDrawCall dc{ range.indexCount, range.indexOffset, range.vertexOffset,
                       2, batchAtlas->descriptorSet(), batchAtlas->distanceRange(), batchBias };
        // First batch goes to the inherited single slot, so untagged text still
        // produces exactly one draw call and costs nothing extra.
        if (emitted == 0) drawCall = dc;
        else              extraDrawCalls_.push_back(dc);
        ++emitted;
        verts.clear();
        indices.clear();
    };

    walkGlyphs(uiMgr_, atlas_, tok, [&](const text::ShapedGlyph& sg) {
        if (sg.isNewline) { penX = originX; penY += lineH; return true; }

        const GlyphInfo* g = sg.glyph;
        if (!g) return true;

        float adv = g->advance * targetPx;

        // Word-wrap: if this glyph overflows the right edge, move to next line.
        if (penX + adv > bMaxX - pad) {
            penX  = originX;
            penY += lineH;
        }
        if (penY > bMaxY - pad) return false;  // baseline past bottom of bounds

        if (g->hasQuad) {
            const float bias = sg.synthesizeBold ? text::kSynthBoldBias : 0.0f;
            if (sg.atlas != batchAtlas || bias != batchBias) {
                flush();
                batchAtlas = sg.atlas;
                batchBias  = bias;
            }

            // planeBounds are em, Y-up, baseline at origin → screen is Y-down,
            // baseline at penY: a point planeT em above baseline sits higher
            // (smaller screen y).
            float xMin = penX + g->planeL * targetPx;
            float xMax = penX + g->planeR * targetPx;
            float yTop = penY - g->planeT * targetPx;   // pair with v0 (atlas top)
            float yBot = penY - g->planeB * targetPx;   // pair with v1 (atlas bottom)

            // Faux italic: lean the quad forward about the baseline. Offset is
            // proportional to height above it, so the baseline itself is fixed
            // and descenders lean back — the pen advance is untouched, matching
            // how real oblique faces are built.
            const float shear = sg.synthesizeItalic ? text::kSynthItalicShear : 0.0f;
            float topShift = shear * (penY - yTop);
            float botShift = shear * (penY - yBot);

            uint32_t base = static_cast<uint32_t>(verts.size());
            verts.push_back({ ndcX(xMin + topShift), ndcY(yTop),  g->u0, g->v0,  cr_, cg_, cb_, ca_ });
            verts.push_back({ ndcX(xMax + topShift), ndcY(yTop),  g->u1, g->v0,  cr_, cg_, cb_, ca_ });
            verts.push_back({ ndcX(xMax + botShift), ndcY(yBot),  g->u1, g->v1,  cr_, cg_, cb_, ca_ });
            verts.push_back({ ndcX(xMin + botShift), ndcY(yBot),  g->u0, g->v1,  cr_, cg_, cb_, ca_ });
            indices.insert(indices.end(), { base, base+1, base+2, base, base+2, base+3 });
        }

        penX += adv;
        return true;
    });
    flush();

    if (emitted == 0) { drawCall = {}; return; }
}
