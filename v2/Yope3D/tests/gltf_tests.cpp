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

// Standard 42-byte buffer: 3 vec3 positions (36B) + 3 uint16 indices (6B),
// returned as a base64 data URI. Shared by the fixtures below.
static std::string triangleBufferUri() {
    std::vector<uint8_t> buf;
    auto pushF = [&](float f) { uint8_t p[4]; std::memcpy(p, &f, 4); for (int i = 0; i < 4; ++i) buf.push_back(p[i]); };
    const float pos[9] = {0,0,0,  1,0,0,  0,1,0};
    for (float f : pos) pushF(f);
    const uint16_t idx[3] = {0, 1, 2};
    for (uint16_t v : idx) { buf.push_back(uint8_t(v & 0xFF)); buf.push_back(uint8_t(v >> 8)); }
    return "data:application/octet-stream;base64," + b64(buf);
}

// Accessors/bufferViews/buffers block shared by every fixture (one triangle).
static std::string sharedTail() {
    return std::string(R"(
      "accessors":[
        {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
        {"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}
      ],
      "bufferViews":[
        {"buffer":0,"byteOffset":0,"byteLength":36},
        {"buffer":0,"byteOffset":36,"byteLength":6}
      ],
      "buffers":[{"byteLength":42,"uri":")") + triangleBufferUri() + R"("}]
    })";
}

static GltfLoader::LoadedModel loadJson(const std::string& json) {
    auto tmp = std::filesystem::temp_directory_path() / "yope_gltf_test.gltf";
    { std::ofstream f(tmp); f << json; }
    auto model = GltfLoader::load(tmp.string());
    std::filesystem::remove(tmp);
    return model;
}

TEST_CASE("glTF node keeps local TRS; verts stay mesh-local (Option B)", "[gltf]") {
    std::string json = std::string(R"({
      "asset":{"version":"2.0"},
      "scene":0,
      "scenes":[{"nodes":[0]}],
      "nodes":[{"mesh":0,"translation":[5,0,0]}],
      "meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],
      "materials":[{"pbrMetallicRoughness":{"baseColorFactor":[1,0,0,1],"metallicFactor":0.25,"roughnessFactor":0.75},"emissiveFactor":[0,1,0]}],)") + sharedTail();

    GltfLoader::LoadedModel model = loadJson(json);

    REQUIRE(model.nodes.size() == 1);
    const GltfLoader::LoadedNode& n = model.nodes[0];

    // Node placement lives on the node's LOCAL transform, not baked into verts.
    CHECK(n.parent == -1);
    CHECK(approx(n.local.position.x, 5.0f));
    CHECK(approx(n.local.position.y, 0.0f));
    CHECK(approx(n.local.scale.x, 1.0f));

    REQUIRE(n.meshes.size() == 1);
    const LoadedMesh& m = n.meshes[0];
    CHECK(m.vertices.size() == 3);
    CHECK(m.indices.size() == 3);

    // Vertices are RAW/local — the (5,0,0) translation is NOT baked in.
    CHECK(approx(m.vertices[0].position[0], 0.0f));
    CHECK(approx(m.vertices[1].position[0], 1.0f));
    CHECK(approx(m.vertices[2].position[1], 1.0f));

    // Material.
    CHECK(m.material.hasMaterial);
    CHECK(approx(m.material.albedoFactor.x, 1.0f));
    CHECK(approx(m.material.albedoFactor.y, 0.0f));
    CHECK(approx(m.material.metallicFactor, 0.25f));
    CHECK(approx(m.material.roughnessFactor, 0.75f));
    CHECK(approx(m.material.emissiveFactor.y, 1.0f));
}

TEST_CASE("glTF nested nodes preserve parent index; locals compose to old world", "[gltf]") {
    // node0 (t=5) -> child node1 (t=2, mesh). Old baked world X was 5+2 = 7.
    std::string json = std::string(R"({
      "asset":{"version":"2.0"},
      "scene":0,
      "scenes":[{"nodes":[0]}],
      "nodes":[
        {"translation":[5,0,0],"children":[1]},
        {"mesh":0,"translation":[2,0,0]}
      ],
      "meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],)") + sharedTail();

    GltfLoader::LoadedModel model = loadJson(json);

    REQUIRE(model.nodes.size() == 2);
    // Topological order: parent precedes child.
    CHECK(model.nodes[0].parent == -1);
    CHECK(model.nodes[0].meshes.empty());
    CHECK(approx(model.nodes[0].local.position.x, 5.0f));

    CHECK(model.nodes[1].parent == 0);
    REQUIRE(model.nodes[1].meshes.size() == 1);
    CHECK(approx(model.nodes[1].local.position.x, 2.0f));

    // Identity rotation/scale => composed world X == sum of local translations.
    float worldX = model.nodes[0].local.position.x + model.nodes[1].local.position.x;
    CHECK(approx(worldX, 7.0f));
}

TEST_CASE("glTF matrix node decomposes to TRS", "[gltf]") {
    // Column-major: scale 2 on the diagonal, translation (1,3,0) in the last column.
    std::string json = std::string(R"({
      "asset":{"version":"2.0"},
      "scene":0,
      "scenes":[{"nodes":[0]}],
      "nodes":[{"mesh":0,"matrix":[2,0,0,0, 0,2,0,0, 0,0,2,0, 1,3,0,1]}],
      "meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],)") + sharedTail();

    GltfLoader::LoadedModel model = loadJson(json);

    REQUIRE(model.nodes.size() == 1);
    const GltfLoader::LoadedNode& n = model.nodes[0];
    CHECK(approx(n.local.position.x, 1.0f));
    CHECK(approx(n.local.position.y, 3.0f));
    CHECK(approx(n.local.scale.x, 2.0f));
    CHECK(approx(n.local.scale.y, 2.0f));
    CHECK(approx(n.local.scale.z, 2.0f));
    // No rotation component.
    CHECK(approx(n.local.rotation.w, 1.0f));
    CHECK(approx(n.local.rotation.x, 0.0f));
}

TEST_CASE("glTF real model (ABeautifulGame) imports as a hierarchy", "[gltf][.real]") {
    // Guarded on the asset being present (it's a large committed .glb). Verifies the
    // Option B contract end-to-end: many nodes, a real parent hierarchy, and meshes
    // whose verts are NOT all pre-baked to the scene origin.
    std::string path = std::string(YOPE_ASSETS_DIR) + "/models/ABeautifulGame.glb";
    if (!std::filesystem::exists(path)) { WARN("ABeautifulGame.glb not present; skipping"); return; }

    GltfLoader::LoadedModel model = GltfLoader::load(path);
    CHECK(model.nodes.size() > 1);

    int parented = 0, withMesh = 0, nonZeroLocal = 0;
    for (const auto& n : model.nodes) {
        if (n.parent >= 0) ++parented;
        if (!n.meshes.empty()) ++withMesh;
        if (std::fabs(n.local.position.x) + std::fabs(n.local.position.y) +
            std::fabs(n.local.position.z) > 1e-4f) ++nonZeroLocal;
    }
    CHECK(withMesh > 0);
    // A chess set is a parent board/group with many child pieces at distinct offsets.
    CHECK(parented > 0);
    CHECK(nonZeroLocal > 0);
}

TEST_CASE("glTF node with multiple primitives yields multiple meshes", "[gltf]") {
    std::string json = std::string(R"({
      "asset":{"version":"2.0"},
      "scene":0,
      "scenes":[{"nodes":[0]}],
      "nodes":[{"mesh":0}],
      "meshes":[{"primitives":[
        {"attributes":{"POSITION":0},"indices":1},
        {"attributes":{"POSITION":0},"indices":1}
      ]}],)") + sharedTail();

    GltfLoader::LoadedModel model = loadJson(json);

    REQUIRE(model.nodes.size() == 1);
    CHECK(model.nodes[0].meshes.size() == 2);
}
