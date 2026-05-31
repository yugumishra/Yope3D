#pragma once
#ifdef YOPE_EDITOR
#include <string>
#include <vector>
#include <map>
#include <variant>
#include <stdexcept>

// Minimal recursive-descent JSON parser.
// Supports object, array, string, number, bool, null.
struct JsonNode {
    enum class Type { Object, Array, String, Number, Bool, Null };

    using ObjectMap   = std::map<std::string, JsonNode>;
    using ArrayVec    = std::vector<JsonNode>;
    using ValueType   = std::variant<std::monostate, ObjectMap, ArrayVec, std::string, double, bool>;

    Type      type  = Type::Null;
    ValueType value;

    // Accessors (throw on type mismatch)
    bool        isNull()   const { return type == Type::Null; }
    bool        isObject() const { return type == Type::Object; }
    bool        isArray()  const { return type == Type::Array; }
    bool        isString() const { return type == Type::String; }
    bool        isNumber() const { return type == Type::Number; }
    bool        isBool()   const { return type == Type::Bool; }

    float       asFloat()  const { return static_cast<float>(std::get<double>(value)); }
    double      asDouble() const { return std::get<double>(value); }
    int         asInt()    const { return static_cast<int>(std::get<double>(value)); }
    unsigned    asUInt()   const { return static_cast<unsigned>(std::get<double>(value)); }
    bool        asBool()   const { return std::get<bool>(value); }
    const std::string& asString() const { return std::get<std::string>(value); }

    const ObjectMap& asObject() const { return std::get<ObjectMap>(value); }
    const ArrayVec&  asArray()  const { return std::get<ArrayVec>(value); }

    // Object field access (returns Null node if key absent)
    const JsonNode& operator[](const std::string& key) const;
    bool contains(const std::string& key) const;

    static const JsonNode& nullNode();
};

// Parse a JSON string. Throws std::runtime_error on parse failure.
JsonNode parseJson(const char* src);
JsonNode parseJsonFile(const char* path);
#endif
