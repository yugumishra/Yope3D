#pragma once
#include <string>
#include <sstream>
#include <cstdint>
#include <cstddef>

// Minimal indented JSON writer. Not a general-purpose library — sized for scene files.
class JsonWriter {
public:
    void beginObject();
    void endObject();
    void beginArray(const char* key);
    void endArray();

    void writeKey  (const char* key);
    void writeString(const char* key, const char* value);
    // Emit a pre-rendered, already-valid JSON fragment as the value for `key`
    // (e.g. a nested object/array or a numeric literal from elsewhere). The
    // caller owns validity — nothing is escaped or quoted.
    void writeRawValue(const char* key, const char* rawJson);
    void writeInt  (const char* key, int value);
    void writeUInt (const char* key, unsigned value);
    void writeFloat(const char* key, float value);
    void writeBool (const char* key, bool value);
    void writeFloat3(const char* key, float x, float y, float z);
    void writeFloat4(const char* key, float x, float y, float z, float w);

    // Write a flat packed array of floats/uints on a single line (used for mesh data).
    void writePackedFloats(const char* key, const float*    data, size_t count);
    void writePackedUInts (const char* key, const uint32_t* data, size_t count);

    // Write a plain string value inside an array (no key).
    void writeArrayString(const char* value);

    // Begin an object in an array (no key)
    void beginArrayObject();

    std::string str() const { return ss_.str(); }

private:
    void indent();
    void comma();

    std::ostringstream ss_;
    int  depth_     = 0;
    bool needComma_ = false;
};
