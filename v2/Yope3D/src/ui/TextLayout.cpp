#include "TextLayout.h"

#include <cctype>

namespace text {

namespace {

// Parses a style tag starting at s[i] (which the caller has checked is '<').
// On success fills `closing`/`style` and sets `next` to the index just past the
// '>'. Returns false for anything that isn't exactly one of the recognized
// tags, in which case the caller keeps the '<' as literal text.
//
// The name scan stops at the first non-letter, so an unterminated '<' costs a
// couple of byte comparisons rather than a scan to end of string.
bool parseStyleTag(const std::string& s, size_t i,
                   bool& closing, TextStyle& style, size_t& next) {
    size_t j = i + 1;
    if (j >= s.size()) return false;

    closing = (s[j] == '/');
    if (closing) ++j;

    const size_t nameStart = j;
    while (j < s.size() && s[j] != '>') {
        const char c = s[j];
        const bool isLetter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (!isLetter) return false;
        ++j;
    }
    if (j >= s.size()) return false;   // ran out before '>' — not a tag

    std::string name = s.substr(nameStart, j - nameStart);
    for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if      (name == "b" || name == "bold")   style = TextStyle::Bold;
    else if (name == "i" || name == "italic") style = TextStyle::Italic;
    else return false;

    next = j + 1;
    return true;
}

} // namespace

char32_t decodeUtf8(const std::string& s, size_t& i) {
    if (i >= s.size()) { ++i; return kReplacementChar; }

    const unsigned char b0 = static_cast<unsigned char>(s[i]);
    if (b0 < 0x80) { ++i; return b0; }        // ASCII

    int      extra;    // continuation bytes this lead byte promises
    char32_t cp;       // accumulator, seeded with the lead byte's payload bits
    char32_t lowest;   // smallest value this length may legally encode
    if      ((b0 & 0xE0) == 0xC0) { extra = 1; cp = b0 & 0x1F; lowest = 0x80;    }
    else if ((b0 & 0xF0) == 0xE0) { extra = 2; cp = b0 & 0x0F; lowest = 0x800;   }
    else if ((b0 & 0xF8) == 0xF0) { extra = 3; cp = b0 & 0x07; lowest = 0x10000; }
    else { ++i; return kReplacementChar; }    // stray continuation, or 5+ byte lead

    if (i + static_cast<size_t>(extra) >= s.size()) { ++i; return kReplacementChar; }

    for (int k = 1; k <= extra; ++k) {
        const unsigned char bk = static_cast<unsigned char>(s[i + static_cast<size_t>(k)]);
        if ((bk & 0xC0) != 0x80) { ++i; return kReplacementChar; }
        cp = (cp << 6) | (bk & 0x3F);
    }

    // Overlong encodings, surrogate halves and out-of-range values are all
    // well-formed bit patterns but illegal Unicode — reject rather than pass a
    // bogus codepoint down to glyph lookup.
    if (cp < lowest || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        ++i;
        return kReplacementChar;
    }

    i += static_cast<size_t>(extra) + 1;
    return cp;
}

std::vector<char32_t> decodeUtf8All(const std::string& s) {
    std::vector<char32_t> out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) out.push_back(decodeUtf8(s, i));
    return out;
}

bool isValidGlyphCodepoint(int code) {
    return code >= 0 && code <= 0x10FFFF;
}

TokenizedText tokenizeStyledText(const std::string& input) {
    TokenizedText out;
    if (input.empty()) return out;

    // Untagged text is the common case by far; skip the scanner entirely.
    if (input.find('<') == std::string::npos) {
        out.plain = input;
        out.runs.push_back({ 0, input.size(), static_cast<uint8_t>(TextStyle::Regular) });
        return out;
    }

    out.plain.reserve(input.size());

    std::vector<TextStyle> stack;   // every tag currently open, innermost last
    uint8_t curStyle = 0;
    size_t  runStart = 0;

    auto currentBits = [&stack]() -> uint8_t {
        uint8_t bits = 0;
        for (TextStyle s : stack) bits |= static_cast<uint8_t>(s);
        return bits;
    };
    auto closeRun = [&](uint8_t newStyle) {
        if (out.plain.size() > runStart)
            out.runs.push_back({ runStart, out.plain.size() - runStart, curStyle });
        runStart = out.plain.size();
        curStyle = newStyle;
    };

    size_t i = 0;
    while (i < input.size()) {
        if (input[i] != '<') { out.plain.push_back(input[i]); ++i; continue; }

        bool      closing = false;
        TextStyle style   = TextStyle::Regular;
        size_t    next    = 0;
        if (!parseStyleTag(input, i, closing, style, next)) {
            out.plain.push_back(input[i]);   // not a tag we know — literal '<'
            ++i;
            continue;
        }

        if (closing) {
            // Unwind the innermost matching opener. Searching rather than just
            // popping keeps improperly nested input (<b><i></b></i>) sane, and
            // a closer with no opener simply finds nothing.
            for (size_t k = stack.size(); k-- > 0;) {
                if (stack[k] == style) { stack.erase(stack.begin() + static_cast<long>(k)); break; }
            }
        } else {
            stack.push_back(style);
        }

        const uint8_t bits = currentBits();
        if (bits != curStyle) closeRun(bits);
        i = next;
    }
    closeRun(0);

    return out;
}

std::string styledFontPath(const std::string& basePath, uint8_t style) {
    if (style == 0) return basePath;

    const char* suffix = "_italic";
    if (hasStyle(style, TextStyle::Bold))
        suffix = hasStyle(style, TextStyle::Italic) ? "_bold_italic" : "_bold";

    // Only treat a '.' as the extension if it comes after the last separator —
    // a dotted directory name must not swallow the suffix.
    const size_t slash = basePath.find_last_of("/\\");
    const size_t dot   = basePath.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return basePath + suffix;

    return basePath.substr(0, dot) + suffix + basePath.substr(dot);
}

} // namespace text
