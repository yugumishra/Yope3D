#include <catch2/catch_test_macros.hpp>
#include "../src/ui/TextLayout.h"
// JsonParser is the front door for all text the engine renders — scene files
// and the baked glyph atlases both come through it — so its \uXXXX handling is
// part of this pipeline and tested alongside it.
#include "../src/scene/serialization/JsonParser.h"

#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// TextLayout — UTF-8 decoding and inline style-tag parsing.
//
// These are the parts of the text pipeline with no atlas or GPU dependency, so
// they're testable headlessly. Glyph rendering, wrapping and the styled-run
// draw-call split are visual and verified in the running engine.
// ---------------------------------------------------------------------------

using namespace text;

namespace {

// Whole-string decode via the index-advancing form, i.e. how callers use it.
std::vector<char32_t> decodeAll(const std::string& s) { return decodeUtf8All(s); }

// Convenience: the style of the run covering byte offset `at`.
uint8_t styleAt(const TokenizedText& tok, size_t at) {
    for (const StyleRun& r : tok.runs)
        if (at >= r.byteOffset && at < r.byteOffset + r.byteLength) return r.style;
    return 0xFF;   // no run covers `at`
}

} // namespace

// ---------------------------------------------------------------------------
// decodeUtf8
// ---------------------------------------------------------------------------

TEST_CASE("decodeUtf8 handles well-formed sequences", "[utf8]") {
    SECTION("empty string yields nothing") {
        REQUIRE(decodeAll("").empty());
    }

    SECTION("ASCII round-trips one byte at a time") {
        const std::string s = "Hi!";
        size_t i = 0;
        REQUIRE(decodeUtf8(s, i) == U'H');  REQUIRE(i == 1);
        REQUIRE(decodeUtf8(s, i) == U'i');  REQUIRE(i == 2);
        REQUIRE(decodeUtf8(s, i) == U'!');  REQUIRE(i == 3);
    }

    SECTION("2-byte sequence (U+00E9 e-acute)") {
        const std::string s = "\xC3\xA9";
        size_t i = 0;
        REQUIRE(decodeUtf8(s, i) == 0x00E9);
        REQUIRE(i == 2);
    }

    SECTION("3-byte sequence (U+20AC euro)") {
        const std::string s = "\xE2\x82\xAC";
        size_t i = 0;
        REQUIRE(decodeUtf8(s, i) == 0x20AC);
        REQUIRE(i == 3);
    }

    SECTION("4-byte sequence (U+1F600 emoji)") {
        const std::string s = "\xF0\x9F\x98\x80";
        size_t i = 0;
        REQUIRE(decodeUtf8(s, i) == 0x1F600);
        REQUIRE(i == 4);
    }

    SECTION("mixed widths decode in order") {
        // "aéz€" — 1, 2, 1, 3 bytes.
        const auto cps = decodeAll("a\xC3\xA9z\xE2\x82\xAC");
        REQUIRE(cps == std::vector<char32_t>{ U'a', 0x00E9, U'z', 0x20AC });
    }

    SECTION("Latin Extended-A boundary (U+017F long s) decodes") {
        const std::string s = "\xC5\xBF";
        size_t i = 0;
        REQUIRE(decodeUtf8(s, i) == 0x017F);
        REQUIRE(i == 2);
    }
}

TEST_CASE("decodeUtf8 rejects malformed input without throwing", "[utf8]") {
    // Every failure mode must return U+FFFD and advance exactly one byte, so a
    // caller looping to size() always terminates and resyncs on the next byte.

    SECTION("index at end of string") {
        const std::string s = "a";
        size_t i = 1;
        REQUIRE(decodeUtf8(s, i) == kReplacementChar);
        REQUIRE(i == 2);
    }

    SECTION("index past end of string") {
        const std::string s = "a";
        size_t i = 99;
        REQUIRE(decodeUtf8(s, i) == kReplacementChar);
        REQUIRE(i == 100);
    }

    SECTION("lone continuation byte") {
        const std::string s = "\xA9";
        size_t i = 0;
        REQUIRE(decodeUtf8(s, i) == kReplacementChar);
        REQUIRE(i == 1);
    }

    SECTION("truncated 2-byte sequence at end of string") {
        const std::string s = "\xC3";
        size_t i = 0;
        REQUIRE(decodeUtf8(s, i) == kReplacementChar);
        REQUIRE(i == 1);
    }

    SECTION("truncated 3-byte sequence at end of string") {
        const std::string s = "\xE2\x82";
        size_t i = 0;
        REQUIRE(decodeUtf8(s, i) == kReplacementChar);
        REQUIRE(i == 1);   // resync is one byte, not the whole bad sequence
        // The orphaned continuation byte then fails on its own terms, so the
        // string yields one replacement per byte rather than swallowing both.
        REQUIRE(decodeUtf8(s, i) == kReplacementChar);
        REQUIRE(i == 2);
        REQUIRE(decodeAll(s).size() == 2);
    }

    SECTION("lead byte followed by a non-continuation byte") {
        const std::string s = "\xC3z";
        size_t i = 0;
        REQUIRE(decodeUtf8(s, i) == kReplacementChar);
        REQUIRE(i == 1);
        REQUIRE(decodeUtf8(s, i) == U'z');   // resynced, 'z' survives
        REQUIRE(i == 2);
    }

    SECTION("overlong encoding of '/' is rejected") {
        const std::string s = "\xC0\xAF";
        size_t i = 0;
        REQUIRE(decodeUtf8(s, i) == kReplacementChar);
        REQUIRE(i == 1);
    }

    SECTION("surrogate half U+D800 is rejected") {
        const std::string s = "\xED\xA0\x80";
        size_t i = 0;
        REQUIRE(decodeUtf8(s, i) == kReplacementChar);
        REQUIRE(i == 1);
    }

    SECTION("value above U+10FFFF is rejected") {
        const std::string s = "\xF7\xBF\xBF\xBF";   // U+1FFFFF
        size_t i = 0;
        REQUIRE(decodeUtf8(s, i) == kReplacementChar);
        REQUIRE(i == 1);
    }

    SECTION("5-byte lead is rejected") {
        const std::string s = "\xF8\x88\x80\x80\x80";
        size_t i = 0;
        REQUIRE(decodeUtf8(s, i) == kReplacementChar);
        REQUIRE(i == 1);
    }

    SECTION("garbage never loops forever and keeps good text") {
        const auto cps = decodeAll("a\xFF\xC3z");
        REQUIRE(cps.size() == 4);
        REQUIRE(cps[0] == U'a');
        REQUIRE(cps[1] == kReplacementChar);
        REQUIRE(cps[2] == kReplacementChar);
        REQUIRE(cps[3] == U'z');
    }
}

// ---------------------------------------------------------------------------
// isValidGlyphCodepoint
// ---------------------------------------------------------------------------

TEST_CASE("isValidGlyphCodepoint bounds", "[utf8]") {
    REQUIRE_FALSE(isValidGlyphCodepoint(-1));
    REQUIRE(isValidGlyphCodepoint(0));
    REQUIRE(isValidGlyphCodepoint(0x7F));
    REQUIRE(isValidGlyphCodepoint(0xE9));    // was dropped by the old ASCII filter
    REQUIRE(isValidGlyphCodepoint(0x17F));   // Latin Extended-A, top of bake range
    REQUIRE(isValidGlyphCodepoint(0x10FFFF));
    REQUIRE_FALSE(isValidGlyphCodepoint(0x110000));
}

// ---------------------------------------------------------------------------
// tokenizeStyledText
// ---------------------------------------------------------------------------

TEST_CASE("tokenizeStyledText on untagged text", "[tags]") {
    SECTION("empty input yields no runs") {
        const auto tok = tokenizeStyledText("");
        REQUIRE(tok.plain.empty());
        REQUIRE(tok.runs.empty());
    }

    SECTION("plain text is one Regular run") {
        const auto tok = tokenizeStyledText("hello");
        REQUIRE(tok.plain == "hello");
        REQUIRE(tok.runs.size() == 1);
        REQUIRE(tok.runs[0].byteOffset == 0);
        REQUIRE(tok.runs[0].byteLength == 5);
        REQUIRE(tok.runs[0].style == static_cast<uint8_t>(TextStyle::Regular));
    }
}

TEST_CASE("tokenizeStyledText on simple tags", "[tags]") {
    SECTION("<b> spans only its contents") {
        const auto tok = tokenizeStyledText("a<b>B</b>c");
        REQUIRE(tok.plain == "aBc");
        REQUIRE(styleAt(tok, 0) == static_cast<uint8_t>(TextStyle::Regular));
        REQUIRE(styleAt(tok, 1) == static_cast<uint8_t>(TextStyle::Bold));
        REQUIRE(styleAt(tok, 2) == static_cast<uint8_t>(TextStyle::Regular));
    }

    SECTION("<i> and the long form <italic> are equivalent") {
        const auto a = tokenizeStyledText("<i>x</i>");
        const auto b = tokenizeStyledText("<italic>x</italic>");
        REQUIRE(a.plain == "x");
        REQUIRE(b.plain == "x");
        REQUIRE(styleAt(a, 0) == static_cast<uint8_t>(TextStyle::Italic));
        REQUIRE(styleAt(b, 0) == static_cast<uint8_t>(TextStyle::Italic));
    }

    SECTION("<bold> long form") {
        const auto tok = tokenizeStyledText("<bold>x</bold>");
        REQUIRE(tok.plain == "x");
        REQUIRE(styleAt(tok, 0) == static_cast<uint8_t>(TextStyle::Bold));
    }

    SECTION("tag names are case-insensitive") {
        const auto tok = tokenizeStyledText("<B>x</b><I>y</I>");
        REQUIRE(tok.plain == "xy");
        REQUIRE(styleAt(tok, 0) == static_cast<uint8_t>(TextStyle::Bold));
        REQUIRE(styleAt(tok, 1) == static_cast<uint8_t>(TextStyle::Italic));
    }

    SECTION("empty tag pair contributes no run") {
        const auto tok = tokenizeStyledText("a<b></b>b");
        REQUIRE(tok.plain == "ab");
        // The zero-length bold region must not produce a phantom run.
        for (const StyleRun& r : tok.runs) REQUIRE(r.byteLength > 0);
    }

    SECTION("runs stay contiguous and cover the whole string") {
        const auto tok = tokenizeStyledText("a<b>B</b>c<i>D</i>e");
        REQUIRE(tok.plain == "aBcDe");
        size_t expect = 0;
        for (const StyleRun& r : tok.runs) {
            REQUIRE(r.byteOffset == expect);
            expect += r.byteLength;
        }
        REQUIRE(expect == tok.plain.size());
    }
}

TEST_CASE("tokenizeStyledText nesting", "[tags]") {
    SECTION("nested tags OR their styles") {
        const auto tok = tokenizeStyledText("<b><i>x</i></b>");
        REQUIRE(tok.plain == "x");
        REQUIRE(styleAt(tok, 0) == (TextStyle::Bold | TextStyle::Italic));
    }

    SECTION("inner close unwinds only the inner style") {
        const auto tok = tokenizeStyledText("<b>A<i>B</i>C</b>");
        REQUIRE(tok.plain == "ABC");
        REQUIRE(styleAt(tok, 0) == static_cast<uint8_t>(TextStyle::Bold));
        REQUIRE(styleAt(tok, 1) == (TextStyle::Bold | TextStyle::Italic));
        REQUIRE(styleAt(tok, 2) == static_cast<uint8_t>(TextStyle::Bold));
    }

    SECTION("improperly crossed nesting still unwinds sanely") {
        const auto tok = tokenizeStyledText("<b>A<i>B</b>C</i>D");
        REQUIRE(tok.plain == "ABCD");
        REQUIRE(styleAt(tok, 0) == static_cast<uint8_t>(TextStyle::Bold));
        REQUIRE(styleAt(tok, 1) == (TextStyle::Bold | TextStyle::Italic));
        REQUIRE(styleAt(tok, 2) == static_cast<uint8_t>(TextStyle::Italic));
        REQUIRE(styleAt(tok, 3) == static_cast<uint8_t>(TextStyle::Regular));
    }

    SECTION("doubled same tag needs both closers") {
        const auto tok = tokenizeStyledText("<b><b>A</b>B</b>C");
        REQUIRE(tok.plain == "ABC");
        REQUIRE(styleAt(tok, 0) == static_cast<uint8_t>(TextStyle::Bold));
        REQUIRE(styleAt(tok, 1) == static_cast<uint8_t>(TextStyle::Bold));
        REQUIRE(styleAt(tok, 2) == static_cast<uint8_t>(TextStyle::Regular));
    }
}

TEST_CASE("tokenizeStyledText is forgiving of malformed markup", "[tags]") {
    SECTION("unknown tag is kept as literal text") {
        const auto tok = tokenizeStyledText("a<foo>b");
        REQUIRE(tok.plain == "a<foo>b");
        REQUIRE(styleAt(tok, 0) == static_cast<uint8_t>(TextStyle::Regular));
    }

    SECTION("unterminated '<' is kept as literal text") {
        const auto tok = tokenizeStyledText("a<b");
        REQUIRE(tok.plain == "a<b");
    }

    SECTION("bare '<' followed by non-letter is literal") {
        const auto tok = tokenizeStyledText("1 < 2");
        REQUIRE(tok.plain == "1 < 2");
    }

    SECTION("unmatched closer is stripped but changes nothing") {
        const auto tok = tokenizeStyledText("a</b>b");
        REQUIRE(tok.plain == "ab");
        REQUIRE(styleAt(tok, 0) == static_cast<uint8_t>(TextStyle::Regular));
        REQUIRE(styleAt(tok, 1) == static_cast<uint8_t>(TextStyle::Regular));
    }

    SECTION("tag left open at end of string just ends") {
        const auto tok = tokenizeStyledText("a<b>B");
        REQUIRE(tok.plain == "aB");
        REQUIRE(styleAt(tok, 0) == static_cast<uint8_t>(TextStyle::Regular));
        REQUIRE(styleAt(tok, 1) == static_cast<uint8_t>(TextStyle::Bold));
    }

    SECTION("empty tag is literal") {
        const auto tok = tokenizeStyledText("a<>b");
        REQUIRE(tok.plain == "a<>b");
    }
}

TEST_CASE("tokenizeStyledText byte offsets survive multi-byte UTF-8", "[tags][utf8]") {
    // "é<b>ñ</b>é" — each accented char is 2 bytes, so a run boundary lands on
    // byte 2 and byte 4, not char 1 and 2. Offsets must be BYTE offsets into
    // plain or the shaper will slice a codepoint in half.
    const auto tok = tokenizeStyledText("\xC3\xA9<b>\xC3\xB1</b>\xC3\xA9");
    REQUIRE(tok.plain == "\xC3\xA9\xC3\xB1\xC3\xA9");
    REQUIRE(tok.plain.size() == 6);

    REQUIRE(styleAt(tok, 0) == static_cast<uint8_t>(TextStyle::Regular));  // é
    REQUIRE(styleAt(tok, 2) == static_cast<uint8_t>(TextStyle::Bold));     // ñ
    REQUIRE(styleAt(tok, 4) == static_cast<uint8_t>(TextStyle::Regular));  // é

    // Every run must start on a codepoint boundary (never a continuation byte).
    for (const StyleRun& r : tok.runs) {
        const unsigned char lead = static_cast<unsigned char>(tok.plain[r.byteOffset]);
        REQUIRE((lead & 0xC0) != 0x80);
    }
}

// ---------------------------------------------------------------------------
// styledFontPath
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// JsonParser \uXXXX escapes
//
// Regression: parseString handled \" \\ \/ \n \r \t and let everything else
// fall through to `default: result += *p`, so "é" lost its backslash and
// reached the renderer as the literal text "u00e9". Writers emit \uXXXX for
// non-ASCII by default (Python's json.dumps, most editors), so every accented
// character authored through a scene file was corrupted.
// ---------------------------------------------------------------------------

TEST_CASE("JsonParser decodes \\uXXXX escapes to UTF-8", "[json][utf8]") {
    auto textOf = [](const char* json) {
        return parseJson(json)["t"].asString();
    };

    SECTION("2-byte codepoint (U+00E9)") {
        REQUIRE(textOf(R"({"t":"Caf\u00e9"})") == "Caf\xC3\xA9");
    }

    SECTION("uppercase hex digits are accepted") {
        REQUIRE(textOf(R"({"t":"Caf\u00E9"})") == "Caf\xC3\xA9");
    }

    SECTION("Latin Extended-A (U+0141 L-stroke)") {
        REQUIRE(textOf(R"({"t":"\u0141od\u017a"})") == "\xC5\x81od\xC5\xBA");
    }

    SECTION("ASCII via escape stays one byte") {
        REQUIRE(textOf(R"({"t":"\u0041"})") == "A");
    }

    SECTION("3-byte codepoint (U+20AC euro)") {
        REQUIRE(textOf(R"({"t":"\u20ac"})") == "\xE2\x82\xAC");
    }

    SECTION("surrogate pair becomes one 4-byte codepoint (U+1F600)") {
        REQUIRE(textOf(R"({"t":"\ud83d\ude00"})") == "\xF0\x9F\x98\x80");
    }

    SECTION("unpaired high surrogate degrades to U+FFFD, not invalid UTF-8") {
        REQUIRE(textOf(R"({"t":"\ud83d"})") == "\xEF\xBF\xBD");
    }

    SECTION("escapes decode inside surrounding text and other escapes") {
        REQUIRE(textOf(R"({"t":"a\u00e9\tb\u00f1"})") == "a\xC3\xA9\tb\xC3\xB1");
    }

    SECTION("the other JSON escapes still work") {
        REQUIRE(textOf(R"({"t":"q\"s\\n\/x\by"})") == "q\"s\\n/x\by");
    }

    SECTION("raw UTF-8 in the source passes through untouched") {
        REQUIRE(textOf("{\"t\":\"Caf\xC3\xA9\"}") == "Caf\xC3\xA9");
    }

    SECTION("decoded escapes survive the text pipeline's own decoder") {
        // The end-to-end contract: JSON escape in, correct codepoint out of the
        // decoder the renderer actually uses for glyph lookup.
        const auto cps = decodeUtf8All(textOf(R"({"t":"\u0141\u00f3d\u017a"})"));
        REQUIRE(cps == std::vector<char32_t>{ 0x0141, 0x00F3, U'd', 0x017A });
    }

    SECTION("malformed escapes are rejected rather than silently mangled") {
        REQUIRE_THROWS(parseJson(R"({"t":"\u00g9"})"));
        REQUIRE_THROWS(parseJson(R"({"t":"\u00"})"));
    }
}

TEST_CASE("styledFontPath follows the on-disk naming convention", "[tags]") {
    const std::string base = "fonts/nunito_sans.ttf";

    REQUIRE(styledFontPath(base, static_cast<uint8_t>(TextStyle::Regular)) == base);
    REQUIRE(styledFontPath(base, static_cast<uint8_t>(TextStyle::Bold))
            == "fonts/nunito_sans_bold.ttf");
    REQUIRE(styledFontPath(base, static_cast<uint8_t>(TextStyle::Italic))
            == "fonts/nunito_sans_italic.ttf");
    REQUIRE(styledFontPath(base, TextStyle::Bold | TextStyle::Italic)
            == "fonts/nunito_sans_bold_italic.ttf");

    SECTION("only the extension dot is split on") {
        REQUIRE(styledFontPath("fonts/nunito.v2.ttf", static_cast<uint8_t>(TextStyle::Bold))
                == "fonts/nunito.v2_bold.ttf");
    }

    SECTION("a dotted directory is not mistaken for an extension") {
        REQUIRE(styledFontPath("fonts.v2/nunito", static_cast<uint8_t>(TextStyle::Bold))
                == "fonts.v2/nunito_bold");
    }

    SECTION("extensionless path just gets the suffix") {
        REQUIRE(styledFontPath("nunito", static_cast<uint8_t>(TextStyle::Italic))
                == "nunito_italic");
    }
}
