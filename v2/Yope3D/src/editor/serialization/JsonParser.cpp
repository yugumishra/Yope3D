#include "editor/serialization/JsonParser.h"
#ifdef YOPE_EDITOR
#include <fstream>
#include <sstream>
#include <cctype>
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
#endif
