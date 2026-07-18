#pragma once
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// TextLayout — pure text-processing helpers shared by the 2D UI text path
// (ui/TextBox) and the 3D world-space text path (Renderer::buildECSText3DGeometry).
//
// This header deliberately depends on nothing but the standard library — no
// Vulkan, no TextAtlas, no UIManager — so it can be unit tested headlessly
// (tests/text_layout_tests.cpp). Anything that needs an atlas or the GPU lives
// in ui/TextShaping instead.
//
// Two concerns live here:
//   1. UTF-8 → codepoint decoding. Text is stored as UTF-8 bytes in char[]
//      component buffers; glyph lookup is keyed by codepoint.
//   2. Inline style tags (<b>/<i>) — tokenized into tag-stripped text plus a
//      list of styled byte ranges.
// ---------------------------------------------------------------------------

namespace text {

// Returned for any malformed sequence; also the standard "unknown character"
// glyph, so it renders as a visible box if the atlas happens to carry it.
inline constexpr char32_t kReplacementChar = 0xFFFD;

// Decodes the codepoint starting at byte index `i` and advances `i` past the
// bytes consumed (1-4 on success).
//
// Never throws and never reads past s.size(). Any malformed input — a lone
// continuation byte, a truncated multi-byte sequence, an overlong encoding, a
// surrogate half, an out-of-range value, or i already at/past the end —
// returns kReplacementChar and advances `i` by exactly one byte, so a caller
// looping to s.size() always terminates and resynchronizes at the next byte.
char32_t decodeUtf8(const std::string& s, size_t& i);

// Decodes the whole string. For callers that walk the text more than once
// (e.g. TextBox's measure-then-emit passes) and would rather not re-decode.
std::vector<char32_t> decodeUtf8All(const std::string& s);

// Whether a codepoint parsed out of a baked atlas's glyph JSON should be kept.
// Rejects negatives and values beyond the Unicode range; the atlas itself
// decides which codepoints it actually carries, this is just a sanity filter.
bool isValidGlyphCodepoint(int code);

// ---------------------------------------------------------------------------
// Inline style tags
// ---------------------------------------------------------------------------

enum class TextStyle : uint8_t {
    Regular = 0,
    Bold    = 1u << 0,
    Italic  = 1u << 1,
};

inline uint8_t operator|(TextStyle a, TextStyle b) {
    return static_cast<uint8_t>(a) | static_cast<uint8_t>(b);
}
inline bool hasStyle(uint8_t bits, TextStyle s) {
    return (bits & static_cast<uint8_t>(s)) != 0;
}

// A maximal run of text sharing one style, as a byte range into
// TokenizedText::plain.
struct StyleRun {
    size_t  byteOffset = 0;
    size_t  byteLength = 0;
    uint8_t style      = 0;   // OR of TextStyle bits
};

struct TokenizedText {
    std::string           plain;   // tag-stripped, still UTF-8
    std::vector<StyleRun> runs;    // sorted, contiguous, covers [0, plain.size())
};

// Scans <b> <bold> <i> <italic> and their closing forms, case-insensitively.
// Tags nest arbitrarily; the style of a run is the OR of every tag open at
// that point, so <b><i>x</i></b> renders x bold-italic.
//
// Forgiving by design — this text comes from a user typing into an ImGui box
// or from hand-edited scene JSON, so nothing here throws or reports an error:
//   - an unrecognized tag ("<foo>")     is kept as literal text, not stripped
//   - an unterminated '<' (no closing   is kept as literal text; scanning
//     '>' before end of string)          resumes at the next byte
//   - an unmatched closer ("</b>" with  is stripped (it is recognized syntax)
//     no matching opener)                but leaves the style stack untouched
//   - tags still open at end of string   are simply dropped
//
// The scan is byte-oriented but UTF-8 safe: continuation bytes are always
// >= 0x80 and so can never collide with '<', '>', or '/'.
TokenizedText tokenizeStyledText(const std::string& input);

// Maps a base font path to its styled variant by filename convention, matching
// the naming already used on disk (nunito_sans.ttf → nunito_sans_bold.ttf):
//   (path, Regular)      → path unchanged
//   (path, Bold)         → "<stem>_bold<ext>"
//   (path, Italic)       → "<stem>_italic<ext>"
//   (path, Bold|Italic)  → "<stem>_bold_italic<ext>"
// Pure string manipulation — whether the variant actually exists on disk is
// TextShaping's problem (see resolveStyledAtlas).
std::string styledFontPath(const std::string& basePath, uint8_t style);

} // namespace text
