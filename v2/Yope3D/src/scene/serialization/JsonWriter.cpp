#include "scene/serialization/JsonWriter.h"
#include <cstdio>
#include <cstdint>

void JsonWriter::indent() {
    for (int i = 0; i < depth_; ++i) ss_ << "    ";
}

void JsonWriter::comma() {
    if (needComma_) ss_ << ",\n";
    needComma_ = false;
}

void JsonWriter::beginObject() {
    comma();
    indent();
    ss_ << "{\n";
    ++depth_;
    needComma_ = false;
}

void JsonWriter::beginArrayObject() {
    comma();
    indent();
    ss_ << "{\n";
    ++depth_;
    needComma_ = false;
}

void JsonWriter::endObject() {
    --depth_;
    ss_ << "\n";
    indent();
    ss_ << "}";
    needComma_ = true;
}

void JsonWriter::beginArray(const char* key) {
    comma();
    indent();
    ss_ << "\"" << key << "\": [\n";
    ++depth_;
    needComma_ = false;
}

void JsonWriter::endArray() {
    --depth_;
    ss_ << "\n";
    indent();
    ss_ << "]";
    needComma_ = true;
}

void JsonWriter::writeKey(const char* key) {
    comma();
    indent();
    ss_ << "\"" << key << "\": ";
    needComma_ = false;
}

void JsonWriter::writeString(const char* key, const char* value) {
    comma();
    indent();
    ss_ << "\"" << key << "\": \"" << (value ? value : "") << "\"";
    needComma_ = true;
}

void JsonWriter::writeInt(const char* key, int value) {
    comma();
    indent();
    ss_ << "\"" << key << "\": " << value;
    needComma_ = true;
}

void JsonWriter::writeUInt(const char* key, unsigned value) {
    comma();
    indent();
    ss_ << "\"" << key << "\": " << value;
    needComma_ = true;
}

void JsonWriter::writeFloat(const char* key, float value) {
    comma();
    indent();
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(value));
    ss_ << "\"" << key << "\": " << buf;
    needComma_ = true;
}

void JsonWriter::writeBool(const char* key, bool value) {
    comma();
    indent();
    ss_ << "\"" << key << "\": " << (value ? "true" : "false");
    needComma_ = true;
}

void JsonWriter::writeFloat3(const char* key, float x, float y, float z) {
    comma();
    indent();
    char buf[128];
    std::snprintf(buf, sizeof(buf), "\"%.6g\", \"%.6g\", \"%.6g\"",
                  (double)x, (double)y, (double)z);
    // Proper JSON array: no quotes around numbers
    std::snprintf(buf, sizeof(buf), "%.6g, %.6g, %.6g",
                  (double)x, (double)y, (double)z);
    ss_ << "\"" << key << "\": [" << buf << "]";
    needComma_ = true;
}

void JsonWriter::writeFloat4(const char* key, float x, float y, float z, float w) {
    comma();
    indent();
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%.6g, %.6g, %.6g, %.6g",
                  (double)x, (double)y, (double)z, (double)w);
    ss_ << "\"" << key << "\": [" << buf << "]";
    needComma_ = true;
}

void JsonWriter::writePackedFloats(const char* key, const float* data, size_t count) {
    comma();
    indent();
    ss_ << "\"" << key << "\": [";
    char buf[32];
    for (size_t i = 0; i < count; ++i) {
        if (i) ss_ << ',';
        std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(data[i]));
        ss_ << buf;
    }
    ss_ << "]";
    needComma_ = true;
}

void JsonWriter::writePackedUInts(const char* key, const uint32_t* data, size_t count) {
    comma();
    indent();
    ss_ << "\"" << key << "\": [";
    for (size_t i = 0; i < count; ++i) {
        if (i) ss_ << ',';
        ss_ << data[i];
    }
    ss_ << "]";
    needComma_ = true;
}
