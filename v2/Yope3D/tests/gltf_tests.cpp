#include <catch2/catch_test_macros.hpp>
#include "../src/assets/GltfLoader.h"
#include <fstream>
#include <filesystem>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>

// Minimal base64 encoder for embedding the binary buffer as a data URI.
static std::string b64(const std::vector<uint8_t>& d) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, bits = -6;
    for (uint8_t c : d) {
        val = (val << 8) + c; bits += 8;
        while (bits >= 0) { out.push_back(T[(val >> bits) & 0x3F]); bits -= 6; }
    }
    if (bits > -6) out.push_back(T[((val << 8) >> (bits + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

static bool approx(float a, float b) { return std::fabs(a - b) < 1e-5f; }

TEST_CASE("glTF loads a triangle primitive + metallic-roughness material", "[gltf]") {
    // Build a 42-byte buffer: 3 vec3 positions (36B) + 3 uint16 indices (6B).
    std::vector<uint8_t> buf;
    auto pushF = [&](float f) { uint8_t p[4]; std::memcpy(p, &f, 4); for (int i = 0; i < 4; ++i) buf.push_back(p[i]); };
    const float pos[9] = {0,0,0,  1,0,0,  0,1,0};
    for (float f : pos) pushF(f);
    const uint16_t idx[3] = {0, 1, 2};
    for (uint16_t v : idx) { buf.push_back(uint8_t(v & 0xFF)); buf.push_back(uint8_t(v >> 8)); }

    std::string uri = "data:application/octet-stream;base64," + b64(buf);

    std::string json = std::string(R"({
      "asset":{"version":"2.0"},
      "scene":0,
      "scenes":[{"nodes":[0]}],
      "nodes":[{"mesh":0,"translation":[5,0,0]}],
      "meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],
      "materials":[{"pbrMetallicRoughness":{"baseColorFactor":[1,0,0,1],"metallicFactor":0.25,"roughnessFactor":0.75},"emissiveFactor":[0,1,0]}],
      "accessors":[
        {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
        {"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}
      ],
      "bufferViews":[
        {"buffer":0,"byteOffset":0,"byteLength":36},
        {"buffer":0,"byteOffset":36,"byteLength":6}
      ],
      "buffers":[{"byteLength":42,"uri":")") + uri + R"("}]
    })";

    auto tmp = std::filesystem::temp_directory_path() / "yope_gltf_test.gltf";
    { std::ofstream f(tmp); f << json; }

    auto meshes = GltfLoader::load(tmp.string());
    std::filesystem::remove(tmp);

    REQUIRE(meshes.size() == 1);
    const LoadedMesh& m = meshes[0];

    CHECK(m.vertices.size() == 3);
    CHECK(m.indices.size() == 3);

    // Node translation (5,0,0) must be baked into vertex positions.
    CHECK(approx(m.vertices[0].position[0], 5.0f));
    CHECK(approx(m.vertices[1].position[0], 6.0f));   // 1 + 5
    CHECK(approx(m.vertices[2].position[1], 1.0f));

    // Material.
    CHECK(m.material.hasMaterial);
    CHECK(approx(m.material.albedoFactor.x, 1.0f));
    CHECK(approx(m.material.albedoFactor.y, 0.0f));
    CHECK(approx(m.material.metallicFactor, 0.25f));
    CHECK(approx(m.material.roughnessFactor, 0.75f));
    CHECK(approx(m.material.emissiveFactor.y, 1.0f));
}
