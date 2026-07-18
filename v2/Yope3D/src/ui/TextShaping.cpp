#include "TextShaping.h"
#include "UIManager.h"

namespace text {

TextAtlas* resolveStyledAtlas(UIManager& uiMgr, const std::string& basePath, uint8_t style,
                              bool* outSynthBold, bool* outSynthItalic) {
    const bool wantBold   = hasStyle(style, TextStyle::Bold);
    const bool wantItalic = hasStyle(style, TextStyle::Italic);

    TextAtlas* atlas  = nullptr;
    bool       synthB = false;
    bool       synthI = false;

    if (!wantBold && !wantItalic) {
        atlas = uiMgr.loadAtlas(basePath);
    } else {
        atlas = uiMgr.loadAtlas(styledFontPath(basePath, style));

        // No family currently ships a bold-italic face, so before giving up on
        // real glyphs entirely, degrade one axis at a time. Real weight with a
        // sheared quad reads better than a faked weight on a real oblique,
        // hence bold first.
        if (!atlas && wantBold && wantItalic) {
            atlas = uiMgr.loadAtlas(styledFontPath(basePath, static_cast<uint8_t>(TextStyle::Bold)));
            if (atlas) {
                synthI = true;
            } else {
                atlas = uiMgr.loadAtlas(styledFontPath(basePath, static_cast<uint8_t>(TextStyle::Italic)));
                if (atlas) synthB = true;
            }
        }

        // Nothing styled is baked for this font (e.g. monaco) — render the base
        // face and fake both axes.
        if (!atlas) {
            atlas  = uiMgr.loadAtlas(basePath);
            synthB = wantBold;
            synthI = wantItalic;
        }
    }

    if (outSynthBold)   *outSynthBold   = atlas ? synthB : false;
    if (outSynthItalic) *outSynthItalic = atlas ? synthI : false;
    return atlas;
}

void shapeTokenized(UIManager& uiMgr, const std::string& baseFontPath,
                    const TokenizedText& tok, const GlyphVisitor& onGlyph) {
    if (!onGlyph) return;

    for (const StyleRun& run : tok.runs) {
        // Resolved once per run, not per glyph — untagged text is a single run.
        bool synthB = false, synthI = false;
        TextAtlas* atlas = resolveStyledAtlas(uiMgr, baseFontPath, run.style, &synthB, &synthI);
        if (!atlas) return;   // base font has no atlas: nothing to draw at all

        const size_t end = run.byteOffset + run.byteLength;
        size_t i = run.byteOffset;
        while (i < end && i < tok.plain.size()) {
            ShapedGlyph sg{};
            sg.codepoint = decodeUtf8(tok.plain, i);
            sg.style     = run.style;

            if (sg.codepoint == U'\n') {
                sg.isNewline = true;
                if (!onGlyph(sg)) return;
                continue;
            }

            sg.atlas           = atlas;
            sg.glyph           = atlas->glyph(sg.codepoint);
            sg.synthesizeBold  = synthB;
            sg.synthesizeItalic = synthI;
            if (!onGlyph(sg)) return;
        }
    }
}

void shapeText(UIManager& uiMgr, const std::string& baseFontPath,
               const std::string& rawText, const GlyphVisitor& onGlyph) {
    shapeTokenized(uiMgr, baseFontPath, tokenizeStyledText(rawText), onGlyph);
}

} // namespace text
