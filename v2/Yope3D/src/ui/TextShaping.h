#pragma once
#include "TextAtlas.h"
#include "TextLayout.h"
#include <functional>
#include <string>

class UIManager;

// ---------------------------------------------------------------------------
// TextShaping — turns a raw text string into a stream of positioned-ready
// glyphs, shared by the 2D UI text path (ui/TextBox) and the 3D world-space
// text path (Renderer::buildECSText3DGeometry).
//
// Shared here: tag parsing, UTF-8 decoding, and per-run atlas resolution.
// NOT shared: pen advance, wrapping, and quad emission — 2D wraps text inside a
// box and pre-measures for centering, 3D lays out a single centered line in
// meters. Those stay with each caller, which is what the visitor callback is
// for: shapeText walks the string and hands back one ShapedGlyph at a time,
// already decoded and already pointed at the right atlas for its style.
//
// A change of ShapedGlyph::atlas between consecutive glyphs is the caller's cue
// to flush its current vertex batch — one atlas means one descriptor set means
// one draw call, so a styled run boundary is necessarily a draw-call boundary.
// ---------------------------------------------------------------------------

namespace text {

struct ShapedGlyph {
    char32_t         codepoint = 0;
    const GlyphInfo* glyph     = nullptr;  // null if the atlas has no such glyph
                                            // (whitespace is non-null, hasQuad=false)
    TextAtlas*       atlas     = nullptr;  // resolved for this glyph's style
    uint8_t          style     = 0;        // active TextStyle bits
    bool             isNewline = false;    // '\n': glyph/atlas unset, reset the pen

    // True when `style` asks for bold but `atlas` is not a real bold face, so
    // the caller should synthesize weight in the shader (see boldBias).
    bool synthesizeBold = false;
    // Likewise for italic: shear the quad geometry, no real oblique face exists.
    bool synthesizeItalic = false;
};

// Return false to stop the walk early (e.g. the 2D box ran out of vertical room).
using GlyphVisitor = std::function<bool(const ShapedGlyph&)>;

// Tokenize tags + decode UTF-8 + resolve atlases, invoking onGlyph per codepoint
// in order. `baseFontPath` is the entity's assigned .ttf path.
void shapeText(UIManager& uiMgr, const std::string& baseFontPath,
               const std::string& rawText, const GlyphVisitor& onGlyph);

// Same, but against text already run through tokenizeStyledText — lets a
// two-pass caller tokenize once and walk twice.
void shapeTokenized(UIManager& uiMgr, const std::string& baseFontPath,
                    const TokenizedText& tok, const GlyphVisitor& onGlyph);

// Resolves the atlas to use for (basePath, style), falling back to the base
// atlas when the styled variant has no baked atlas. `outSynthBold` /
// `outSynthItalic` report whether the caller must fake that style because the
// fallback fired. Returns nullptr only if the base font itself fails to load.
TextAtlas* resolveStyledAtlas(UIManager& uiMgr, const std::string& basePath, uint8_t style,
                              bool* outSynthBold = nullptr, bool* outSynthItalic = nullptr);

// How much to bias the MSDF median-distance threshold when faking bold, in
// distance-field units. Pushed to the text shaders as `boldBias`.
inline constexpr float kSynthBoldBias = 0.055f;

// Horizontal shear (x += kSynthItalicShear * heightAboveBaseline) when faking
// italic. ~12 degrees, close to typical oblique designs.
inline constexpr float kSynthItalicShear = 0.21f;

} // namespace text
