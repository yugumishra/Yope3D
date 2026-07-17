#include "scene/serialization/JsonParser.h"
#include <fstream>
#include <sstream>
#include <cctype>
#include <cstdint>
#include <cstdlib>

static const JsonNode g_nullNode;

const JsonNode& JsonNode::nullNode() { return g_nullNode; }

const JsonNode& JsonNode::operator[](const std::string& key) const {
    if (type != Type::Object) return nullNode();
    auto& map = std::get<ObjectMap>(value);
    auto it = map.find(key);
    return (it != map.end()) ? it->second : nullNode();
}

bool JsonNode::contains(const std::string& key) const {
    if (type != Type::Object) return false;
    return std::get<ObjectMap>(value).count(key) > 0;
}

namespace {

// Reads exactly 4 hex digits (the payload of a \uXXXX escape) starting at `h`.
// The caller must have already checked that 4 bytes are readable.
uint32_t parseHex4(const char* h) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        const char c = h[i];
        v <<= 4;
        if      (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
        else throw std::runtime_error("JSON: bad hex digit in \\u escape");
    }
    return v;
}

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp <= 0x7F) {
        out += static_cast<char>(cp);
    } else if (cp <= 0x7FF) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Parser state
// ---------------------------------------------------------------------------

struct ParseState {
    const char* p;
    const char* end;

    void skipWs() {
        while (p < end && std::isspace(static_cast<unsigned char>(*p))) ++p;
    }

    char peek() { skipWs(); return (p < end) ? *p : '\0'; }
    char consume() { return (p < end) ? *p++ : '\0'; }

    void expect(char c) {
        skipWs();
        if (p >= end || *p != c)
            throw std::runtime_error(std::string("JSON: expected '") + c + "', got '" + (p < end ? *p : '?') + "'");
        ++p;
    }

    JsonNode parseValue();
    JsonNode parseObject();
    JsonNode parseArray();
    std::string parseString();
    JsonNode parseNumber();
};

std::string ParseState::parseString() {
    expect('"');
    std::string result;
    while (p < end && *p != '"') {
        if (*p == '\\') {
            ++p;
            if (p >= end) throw std::runtime_error("JSON: unexpected end in escape");
            switch (*p) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'u': {
                    // \uXXXX → UTF-8. Required for non-ASCII text: writers emit
                    // these by default (Python's json.dumps, most editors), and
                    // without this the escape decayed to the literal text "u00e9".
                    if (end - p < 5) throw std::runtime_error("JSON: truncated \\u escape");
                    uint32_t cp = parseHex4(p + 1);
                    p += 4;   // sit on the last hex digit; the loop's ++p steps past it

                    // Astral codepoints arrive as a surrogate pair.
                    if (cp >= 0xD800 && cp <= 0xDBFF && (end - p) >= 7 &&
                        p[1] == '\\' && p[2] == 'u') {
                        const uint32_t lo = parseHex4(p + 3);
                        if (lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            p += 6;
                        }
                    }
                    // An unpaired surrogate can't be encoded; substitute rather
                    // than emit invalid UTF-8 for the text layer to choke on.
                    if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
                    appendUtf8(result, cp);
                    break;
                }
                default: result += *p; break;
            }
        } else {
            result += *p;
        }
        ++p;
    }
    expect('"');
    return result;
}

JsonNode ParseState::parseNumber() {
    skipWs();
    const char* start = p;
    if (p < end && (*p == '-' || *p == '+')) ++p;
    while (p < end && std::isdigit(static_cast<unsigned char>(*p))) ++p;
    if (p < end && *p == '.') { ++p; while (p < end && std::isdigit(static_cast<unsigned char>(*p))) ++p; }
    if (p < end && (*p == 'e' || *p == 'E')) {
        ++p;
        if (p < end && (*p == '-' || *p == '+')) ++p;
        while (p < end && std::isdigit(static_cast<unsigned char>(*p))) ++p;
    }
    char* endPtr;
    double val = std::strtod(start, &endPtr);
    JsonNode n;
    n.type  = JsonNode::Type::Number;
    n.value = val;
    return n;
}

JsonNode ParseState::parseObject() {
    expect('{');
    JsonNode::ObjectMap map;
    skipWs();
    if (peek() == '}') { consume(); JsonNode n; n.type = JsonNode::Type::Object; n.value = std::move(map); return n; }
    while (true) {
        skipWs();
        std::string key = parseString();
        expect(':');
        map[key] = parseValue();
        skipWs();
        if (peek() == '}') { consume(); break; }
        expect(',');
    }
    JsonNode n; n.type = JsonNode::Type::Object; n.value = std::move(map);
    return n;
}

JsonNode ParseState::parseArray() {
    expect('[');
    JsonNode::ArrayVec arr;
    skipWs();
    if (peek() == ']') { consume(); JsonNode n; n.type = JsonNode::Type::Array; n.value = std::move(arr); return n; }
    while (true) {
        arr.push_back(parseValue());
        skipWs();
        if (peek() == ']') { consume(); break; }
        expect(',');
    }
    JsonNode n; n.type = JsonNode::Type::Array; n.value = std::move(arr);
    return n;
}

JsonNode ParseState::parseValue() {
    char c = peek();
    if (c == '{') return parseObject();
    if (c == '[') return parseArray();
    if (c == '"') { std::string s = parseString(); JsonNode n; n.type = JsonNode::Type::String; n.value = std::move(s); return n; }
    if (c == 't') { p += 4; JsonNode n; n.type = JsonNode::Type::Bool; n.value = true;  return n; }
    if (c == 'f') { p += 5; JsonNode n; n.type = JsonNode::Type::Bool; n.value = false; return n; }
    if (c == 'n') { p += 4; return g_nullNode; }
    return parseNumber();
}

JsonNode parseJson(const char* src) {
    ParseState ps{src, src + std::strlen(src)};
    return ps.parseValue();
}

JsonNode parseJsonFile(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error(std::string("parseJsonFile: cannot open: ") + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    return parseJson(s.c_str());
}
