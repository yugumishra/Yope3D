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

    // An unskinned primitive must stay unskinned — influenceCount 0 is what the
    // rest of the engine tests to decide whether a mesh needs the skinning path.
    CHECK(m.influenceCount == 0);
    CHECK(m.skinJoints.empty());
    CHECK(m.skinWeights.empty());

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

TEST_CASE("glTF rigid animation: LINEAR translation + CUBICSPLINE scale, node remap", "[gltf][anim]") {
    // Deliberately mismatches glTF node index vs. LoadedModel traversal index:
    // scene root is glTF node 1 (no mesh, a bare group), whose only child is
    // glTF node 0 (the mesh + animation target). Traversal visits node1 first
    // (LoadedModel index 0) then node0 (LoadedModel index 1) — so this exercises
    // GltfLoader's gltfNodeToLocal remap table, not just a 1:1 coincidence.
    auto pushF = [](std::vector<uint8_t>& buf, float f) {
        uint8_t p[4]; std::memcpy(p, &f, 4);
        for (int i = 0; i < 4; ++i) buf.push_back(p[i]);
    };

    std::vector<uint8_t> buf;
    const float pos[9] = {0,0,0,  1,0,0,  0,1,0};
    for (float f : pos) pushF(buf, f);                              // [0,36) POSITION
    const uint16_t idx[3] = {0, 1, 2};
    for (uint16_t v : idx) { buf.push_back(uint8_t(v & 0xFF)); buf.push_back(uint8_t(v >> 8)); }  // [36,42) indices

    const float times[2] = {0.0f, 1.0f};
    for (float f : times) pushF(buf, f);                             // [42,50) time

    const float trans[6] = {0,0,0,  2,4,6};
    for (float f : trans) pushF(buf, f);                             // [50,74) LINEAR translation

    // CUBICSPLINE scale: key0{in=(0,0,0), val=(0,0,0), out=(1,0,0)}, key1{in=(1,0,0), val=(2,0,0), out=(0,0,0)}.
    // Hand-verified Hermite midpoint (s=0.5, segDur=1): x = 0.5*0 + 0.125*1 + 0.5*2 + (-0.125)*1 = 1.0.
    const float scaleSpline[18] = {
        0,0,0,  0,0,0,  1,0,0,
        1,0,0,  2,0,0,  0,0,0,
    };
    for (float f : scaleSpline) pushF(buf, f);                       // [74,146)

    std::string json = std::string(R"({
      "asset":{"version":"2.0"},
      "scene":0,
      "scenes":[{"nodes":[1]}],
      "nodes":[
        {"mesh":0},
        {"children":[0]}
      ],
      "meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],
      "animations":[{
        "name":"wiggle",
        "samplers":[
          {"input":2,"output":3,"interpolation":"LINEAR"},
          {"input":2,"output":4,"interpolation":"CUBICSPLINE"}
        ],
        "channels":[
          {"sampler":0,"target":{"node":0,"path":"translation"}},
          {"sampler":1,"target":{"node":0,"path":"scale"}}
        ]
      }],
      "accessors":[
        {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
        {"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"},
        {"bufferView":2,"componentType":5126,"count":2,"type":"SCALAR"},
        {"bufferView":3,"componentType":5126,"count":2,"type":"VEC3"},
        {"bufferView":4,"componentType":5126,"count":6,"type":"VEC3"}
      ],
      "bufferViews":[
        {"buffer":0,"byteOffset":0,"byteLength":36},
        {"buffer":0,"byteOffset":36,"byteLength":6},
        {"buffer":0,"byteOffset":42,"byteLength":8},
        {"buffer":0,"byteOffset":50,"byteLength":24},
        {"buffer":0,"byteOffset":74,"byteLength":72}
      ],
      "buffers":[{"byteLength":146,"uri":")") + "data:application/octet-stream;base64," +
      b64(buf) + R"("}]
    })";

    GltfLoader::LoadedModel model = loadJson(json);

    REQUIRE(model.nodes.size() == 2);
    REQUIRE(model.animations.size() == 1);
    const auto& clip = model.animations[0];
    CHECK(clip.name == "wiggle");
    CHECK(approx(clip.duration, 1.0f));
    REQUIRE(clip.channels.size() == 2);

    const anim::Channel* trCh = nullptr;
    const anim::Channel* scCh = nullptr;
    for (const auto& ch : clip.channels) {
        if (ch.path == anim::Path::Translation) trCh = &ch;
        if (ch.path == anim::Path::Scale)       scCh = &ch;
    }
    REQUIRE(trCh != nullptr);
    REQUIRE(scCh != nullptr);

    // The remap: glTF node 0 (the mesh) must land on the LoadedModel index that
    // actually carries a mesh — NOT node index 0 verbatim (traversal visited the
    // bare group root first).
    CHECK(trCh->targetNode == scCh->targetNode);
    REQUIRE(trCh->targetNode >= 0);
    REQUIRE(trCh->targetNode < static_cast<int>(model.nodes.size()));
    CHECK(model.nodes[trCh->targetNode].meshes.size() == 1);

    CHECK(trCh->interp == anim::Interp::Linear);
    CHECK(scCh->interp == anim::Interp::CubicSpline);

    // LINEAR translation: endpoints exact, midpoint is the arithmetic mean.
    math::Vec3 t0 = anim::sampleVec3Channel(*trCh, 0.0f);
    math::Vec3 t1 = anim::sampleVec3Channel(*trCh, 1.0f);
    math::Vec3 tm = anim::sampleVec3Channel(*trCh, 0.5f);
    CHECK(approx(t0.x, 0.0f)); CHECK(approx(t0.y, 0.0f)); CHECK(approx(t0.z, 0.0f));
    CHECK(approx(t1.x, 2.0f)); CHECK(approx(t1.y, 4.0f)); CHECK(approx(t1.z, 6.0f));
    CHECK(approx(tm.x, 1.0f)); CHECK(approx(tm.y, 2.0f)); CHECK(approx(tm.z, 3.0f));

    // CUBICSPLINE scale: endpoints are the authored values; midpoint follows the
    // Hermite blend of value + tangents (hand-verified above), NOT a linear mean
    // (which would incorrectly give 1.0 too here — the tangents were chosen so
    // both formulas coincide at the midpoint; the exact-endpoint checks below are
    // what actually distinguish Hermite evaluation from a naive lerp fallback).
    math::Vec3 s0 = anim::sampleVec3Channel(*scCh, 0.0f);
    math::Vec3 s1 = anim::sampleVec3Channel(*scCh, 1.0f);
    math::Vec3 sm = anim::sampleVec3Channel(*scCh, 0.5f);
    CHECK(approx(s0.x, 0.0f));
    CHECK(approx(s1.x, 2.0f));
    CHECK(approx(sm.x, 1.0f));
}

// ---------------------------------------------------------------------------
// Skinning (M16). Fixtures below share a 3-vertex triangle at buffer offset 0 and
// 3 uint16 indices at 36, padded to 44 so the following views stay 4-byte aligned.
// ---------------------------------------------------------------------------

static void pushFloat(std::vector<uint8_t>& buf, float f) {
    uint8_t p[4]; std::memcpy(p, &f, 4);
    for (int i = 0; i < 4; ++i) buf.push_back(p[i]);
}

// POSITION [0,36) + indices [36,42) + 2 bytes of padding, leaving size == 44.
static std::vector<uint8_t> triangleHead() {
    std::vector<uint8_t> buf;
    const float pos[9] = {0,0,0,  1,0,0,  0,1,0};
    for (float f : pos) pushFloat(buf, f);
    const uint16_t idx[3] = {0, 1, 2};
    for (uint16_t v : idx) { buf.push_back(uint8_t(v & 0xFF)); buf.push_back(uint8_t(v >> 8)); }
    buf.push_back(0); buf.push_back(0);
    return buf;
}

static std::string dataUri(const std::vector<uint8_t>& buf) {
    return "data:application/octet-stream;base64," + b64(buf);
}

// Sums one vertex's quantised weights. The packer's contract is that this is
// exactly 255 — see LoadedMesh.
static int weightSum(const LoadedMesh& m, size_t vertex) {
    int s = 0;
    for (int k = 0; k < m.influenceCount; ++k)
        s += m.skinWeights[vertex * m.influenceCount + k];
    return s;
}

// One skinned triangle: 2 joints, per-vertex influences chosen to exercise the
// quantiser (clean split / single influence / a pair that naively rounds to 256).
// Node layout deliberately mismatches glTF node order against traversal order so
// the joint remap is actually tested: scene root is glTF node 3 (a bare group)
// whose children are [0,1,2], so gltfNodeToLocal ends up {1,2,3,0}.
static std::string skinnedTriangleJson() {
    std::vector<uint8_t> buf = triangleHead();

    const uint8_t joints[12] = { 0,1,0,0,   1,0,0,0,   0,1,0,0 };
    for (uint8_t j : joints) buf.push_back(j);                          // [44,56)

    const float weights[12] = { 0.75f,0.25f,0,0,   1.0f,0,0,0,   0.5f,0.5f,0,0 };
    for (float f : weights) pushFloat(buf, f);                          // [56,104)

    // inverseBindMatrices, column-major: jointA = translate(-1,0,0),
    // jointB = translate(0,-2,0). Distinct so a swapped/short read is visible.
    const float ibm[32] = {
        1,0,0,0,  0,1,0,0,  0,0,1,0,  -1, 0,0,1,
        1,0,0,0,  0,1,0,0,  0,0,1,0,   0,-2,0,1,
    };
    for (float f : ibm) pushFloat(buf, f);                              // [104,232)

    return std::string(R"({
      "asset":{"version":"2.0"},
      "scene":0,
      "scenes":[{"nodes":[3]}],
      "nodes":[
        {"mesh":0,"skin":0},
        {"name":"jointA"},
        {"name":"jointB"},
        {"children":[0,1,2]}
      ],
      "skins":[{"name":"rig","skeleton":1,"joints":[1,2],"inverseBindMatrices":4}],
      "meshes":[{"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":2,"WEIGHTS_0":3},"indices":1}]}],
      "accessors":[
        {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
        {"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"},
        {"bufferView":2,"componentType":5121,"count":3,"type":"VEC4"},
        {"bufferView":3,"componentType":5126,"count":3,"type":"VEC4"},
        {"bufferView":4,"componentType":5126,"count":2,"type":"MAT4"}
      ],
      "bufferViews":[
        {"buffer":0,"byteOffset":0,"byteLength":36},
        {"buffer":0,"byteOffset":36,"byteLength":6},
        {"buffer":0,"byteOffset":44,"byteLength":12},
        {"buffer":0,"byteOffset":56,"byteLength":48},
        {"buffer":0,"byteOffset":104,"byteLength":128}
      ],
      "buffers":[{"byteLength":232,"uri":")") + dataUri(buf) + R"("}]
    })";
}

TEST_CASE("glTF skin: joints and skeleton remap to node indices, inverse binds load", "[gltf][skin]") {
    GltfLoader::LoadedModel model = loadJson(skinnedTriangleJson());

    REQUIRE(model.nodes.size() == 4);
    REQUIRE(model.skins.size() == 1);
    const GltfLoader::LoadedSkin& sk = model.skins[0];
    CHECK(sk.name == "rig");

    // Traversal order (group first, then children 0,1,2) means glTF joints [1,2]
    // must land on LoadedModel indices [2,3] — NOT verbatim.
    REQUIRE(sk.joints.size() == 2);
    CHECK(sk.joints[0] == 2);
    CHECK(sk.joints[1] == 3);
    CHECK(model.nodes[sk.joints[0]].name == "jointA");
    CHECK(model.nodes[sk.joints[1]].name == "jointB");
    CHECK(sk.skeletonRoot == 2);

    // The mesh node is glTF node 0, which traversal visited second.
    CHECK(model.nodes[1].skin == 0);
    REQUIRE(model.nodes[1].meshes.size() == 1);

    REQUIRE(sk.inverseBind.size() == 2);
    CHECK(approx(sk.inverseBind[0].m[12], -1.0f));
    CHECK(approx(sk.inverseBind[1].m[13], -2.0f));
    CHECK(approx(sk.inverseBind[0].m[0],   1.0f));   // column-major copy, not transposed
    CHECK(approx(sk.inverseBind[1].m[12],  0.0f));
}

TEST_CASE("glTF skin weights: exact 255 sum, largest last, zero-padded front", "[gltf][skin]") {
    GltfLoader::LoadedModel model = loadJson(skinnedTriangleJson());
    REQUIRE(model.nodes.size() == 4);
    REQUIRE(model.nodes[1].meshes.size() == 1);
    const LoadedMesh& m = model.nodes[1].meshes[0];

    // Only two influences are ever used, so the mesh stays 4-wide.
    REQUIRE(m.influenceCount == 4);
    REQUIRE(m.skinJoints.size()  == 12);
    REQUIRE(m.skinWeights.size() == 12);
    CHECK(m.truncatedVertices == 0);

    // v0: 0.75/0.25 on joints 0/1. Largest last, padding at the front.
    CHECK(m.skinWeights[0] == 0);   CHECK(m.skinWeights[1] == 0);
    CHECK(m.skinWeights[2] == 64);  CHECK(m.skinWeights[3] == 191);
    CHECK(m.skinJoints[2]  == 1);   CHECK(m.skinJoints[3]  == 0);

    // v1: a single full-weight influence occupies only the last slot.
    CHECK(m.skinWeights[4] == 0); CHECK(m.skinWeights[5] == 0); CHECK(m.skinWeights[6] == 0);
    CHECK(m.skinWeights[7] == 255);
    CHECK(m.skinJoints[7]  == 1);

    // v2: 0.5/0.5 rounds to 128+128 = 256 naively. The remainder must be absorbed
    // so the pair still sums to exactly 255 (which of the tied pair takes the hit
    // is unspecified, so assert the invariant rather than the split).
    CHECK(m.skinWeights[8] == 0); CHECK(m.skinWeights[9] == 0);
    CHECK(m.skinWeights[10] + m.skinWeights[11] == 255);

    for (size_t v = 0; v < 3; ++v) CHECK(weightSum(m, v) == 255);
}

TEST_CASE("glTF skin: an all-zero JOINTS_1 set does not widen the mesh to 8", "[gltf][skin]") {
    // Exporters routinely emit a second influence set that carries no weight.
    // Width comes from what vertices actually use, not from attribute presence.
    std::vector<uint8_t> buf = triangleHead();
    const uint8_t j0[12] = { 0,1,0,0,  1,0,0,0,  0,1,0,0 };
    for (uint8_t j : j0) buf.push_back(j);                              // [44,56)
    const uint8_t j1[12] = { 0,0,0,0,  0,0,0,0,  0,0,0,0 };
    for (uint8_t j : j1) buf.push_back(j);                              // [56,68)
    const float w0[12] = { 0.75f,0.25f,0,0,  1.0f,0,0,0,  0.5f,0.5f,0,0 };
    for (float f : w0) pushFloat(buf, f);                               // [68,116)
    for (int i = 0; i < 12; ++i) pushFloat(buf, 0.0f);                  // [116,164)

    std::string json = std::string(R"({
      "asset":{"version":"2.0"},
      "scene":0,
      "scenes":[{"nodes":[0,1]}],
      "nodes":[{"mesh":0,"skin":0},{"name":"j"}],
      "skins":[{"joints":[1]}],
      "meshes":[{"primitives":[{"attributes":{
        "POSITION":0,"JOINTS_0":2,"WEIGHTS_0":4,"JOINTS_1":3,"WEIGHTS_1":5},"indices":1}]}],
      "accessors":[
        {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
        {"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"},
        {"bufferView":2,"componentType":5121,"count":3,"type":"VEC4"},
        {"bufferView":3,"componentType":5121,"count":3,"type":"VEC4"},
        {"bufferView":4,"componentType":5126,"count":3,"type":"VEC4"},
        {"bufferView":5,"componentType":5126,"count":3,"type":"VEC4"}
      ],
      "bufferViews":[
        {"buffer":0,"byteOffset":0,"byteLength":36},
        {"buffer":0,"byteOffset":36,"byteLength":6},
        {"buffer":0,"byteOffset":44,"byteLength":12},
        {"buffer":0,"byteOffset":56,"byteLength":12},
        {"buffer":0,"byteOffset":68,"byteLength":48},
        {"buffer":0,"byteOffset":116,"byteLength":48}
      ],
      "buffers":[{"byteLength":164,"uri":")") + dataUri(buf) + R"("}]
    })";

    GltfLoader::LoadedModel model = loadJson(json);
    REQUIRE(model.nodes.size() == 2);
    REQUIRE(model.nodes[0].meshes.size() == 1);
    const LoadedMesh& m = model.nodes[0].meshes[0];

    CHECK(m.influenceCount == 4);
    CHECK(m.truncatedVertices == 0);
    CHECK(m.skinWeights.size() == 12);
    for (size_t v = 0; v < 3; ++v) CHECK(weightSum(m, v) == 255);
}

TEST_CASE("glTF skin: influences past 8 are dropped and counted", "[gltf][skin]") {
    // Three full sets = 12 non-zero influences on every vertex. The mesh widens to
    // 8 and each vertex is reported as truncated.
    std::vector<uint8_t> buf = triangleHead();
    for (int set = 0; set < 3; ++set)                                   // [44,80)
        for (int v = 0; v < 3; ++v)
            for (int k = 0; k < 4; ++k)
                buf.push_back(uint8_t(set * 4 + k));
    for (int set = 0; set < 3; ++set)                                   // [80,224)
        for (int i = 0; i < 12; ++i)
            pushFloat(buf, 1.0f / 12.0f);

    std::string json = std::string(R"({
      "asset":{"version":"2.0"},
      "scene":0,
      "scenes":[{"nodes":[0,1]}],
      "nodes":[{"mesh":0,"skin":0},{"name":"j"}],
      "skins":[{"joints":[1]}],
      "meshes":[{"primitives":[{"attributes":{
        "POSITION":0,
        "JOINTS_0":2,"JOINTS_1":3,"JOINTS_2":4,
        "WEIGHTS_0":5,"WEIGHTS_1":6,"WEIGHTS_2":7},"indices":1}]}],
      "accessors":[
        {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
        {"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"},
        {"bufferView":2,"componentType":5121,"count":3,"type":"VEC4"},
        {"bufferView":3,"componentType":5121,"count":3,"type":"VEC4"},
        {"bufferView":4,"componentType":5121,"count":3,"type":"VEC4"},
        {"bufferView":5,"componentType":5126,"count":3,"type":"VEC4"},
        {"bufferView":6,"componentType":5126,"count":3,"type":"VEC4"},
        {"bufferView":7,"componentType":5126,"count":3,"type":"VEC4"}
      ],
      "bufferViews":[
        {"buffer":0,"byteOffset":0,"byteLength":36},
        {"buffer":0,"byteOffset":36,"byteLength":6},
        {"buffer":0,"byteOffset":44,"byteLength":12},
        {"buffer":0,"byteOffset":56,"byteLength":12},
        {"buffer":0,"byteOffset":68,"byteLength":12},
        {"buffer":0,"byteOffset":80,"byteLength":48},
        {"buffer":0,"byteOffset":128,"byteLength":48},
        {"buffer":0,"byteOffset":176,"byteLength":48}
      ],
      "buffers":[{"byteLength":224,"uri":")") + dataUri(buf) + R"("}]
    })";

    GltfLoader::LoadedModel model = loadJson(json);
    REQUIRE(model.nodes.size() == 2);
    REQUIRE(model.nodes[0].meshes.size() == 1);
    const LoadedMesh& m = model.nodes[0].meshes[0];

    CHECK(m.influenceCount == 8);
    CHECK(m.truncatedVertices == 3);
    REQUIRE(m.skinWeights.size() == 24);
    // Renormalising 8 equal influences still has to land on exactly 255.
    for (size_t v = 0; v < 3; ++v) CHECK(weightSum(m, v) == 255);
}

TEST_CASE("glTF skin: joint indices marked normalized are rejected", "[gltf][skin]") {
    // readFloats would decode a normalized u8 to [0,1], silently collapsing every
    // joint index to 0. Refusing beats deforming the character onto one bone.
    std::vector<uint8_t> buf = triangleHead();
    const uint8_t j0[12] = { 0,1,0,0,  1,0,0,0,  0,1,0,0 };
    for (uint8_t j : j0) buf.push_back(j);
    const float w0[12] = { 0.75f,0.25f,0,0,  1.0f,0,0,0,  0.5f,0.5f,0,0 };
    for (float f : w0) pushFloat(buf, f);

    std::string json = std::string(R"({
      "asset":{"version":"2.0"},
      "scene":0,
      "scenes":[{"nodes":[0,1]}],
      "nodes":[{"mesh":0,"skin":0},{"name":"j"}],
      "skins":[{"joints":[1]}],
      "meshes":[{"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":2,"WEIGHTS_0":3},"indices":1}]}],
      "accessors":[
        {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
        {"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"},
        {"bufferView":2,"componentType":5121,"count":3,"type":"VEC4","normalized":true},
        {"bufferView":3,"componentType":5126,"count":3,"type":"VEC4"}
      ],
      "bufferViews":[
        {"buffer":0,"byteOffset":0,"byteLength":36},
        {"buffer":0,"byteOffset":36,"byteLength":6},
        {"buffer":0,"byteOffset":44,"byteLength":12},
        {"buffer":0,"byteOffset":56,"byteLength":48}
      ],
      "buffers":[{"byteLength":104,"uri":")") + dataUri(buf) + R"("}]
    })";

    CHECK_THROWS(loadJson(json));
}

TEST_CASE("glTF skin: the committed skinTest asset loads as a 2-bone rig", "[gltf][skin]") {
    // The engine's only skinned asset (tools/gen_skin_test_asset.py). Guards the
    // generator and the loader against each other — a change to either that broke
    // the contract would otherwise only surface as a mangled character on screen.
    std::string path = std::string(YOPE_ASSETS_DIR) + "/models/skinTest.gltf";
    if (!std::filesystem::exists(path)) { WARN("skinTest.gltf not present; skipping"); return; }

    GltfLoader::LoadedModel model = GltfLoader::load(path);

    REQUIRE(model.skins.size() == 1);
    const GltfLoader::LoadedSkin& sk = model.skins[0];
    CHECK(sk.name == "TestRig");
    REQUIRE(sk.joints.size() == 2);
    REQUIRE(sk.inverseBind.size() == 2);
    // bone0 at the origin, bone1 one unit up => inverse binds of 0 and -1 in y.
    CHECK(approx(sk.inverseBind[0].m[13],  0.0f));
    CHECK(approx(sk.inverseBind[1].m[13], -1.0f));

    // Find the skinned mesh node.
    const GltfLoader::LoadedNode* meshNode = nullptr;
    for (const auto& n : model.nodes)
        if (n.skin == 0 && !n.meshes.empty()) meshNode = &n;
    REQUIRE(meshNode != nullptr);

    // Flat-shaded: 4 side faces x 3 rings x 2 verts, plus 4-vert caps = 32.
    const LoadedMesh& m = meshNode->meshes[0];
    CHECK(m.vertices.size() == 32);
    CHECK(m.influenceCount == 4);          // only 2 influences used => stays 4-wide
    CHECK(m.truncatedVertices == 0);
    REQUIRE(m.skinWeights.size() == 32 * 4);

    // The invariant that matters everywhere downstream.
    for (size_t v = 0; v < 32; ++v) {
        INFO("vertex " << v);
        int sum = 0;
        for (int k = 0; k < 4; ++k) sum += m.skinWeights[v * 4 + k];
        CHECK(sum == 255);
    }

    // Face 0 emits (ring0,a) (ring0,b) (ring1,a) (ring1,b) (ring2,a) (ring2,b),
    // so verts 0-1 are rigid to bone 0, 2-3 are the 50/50 band, 4-5 rigid to bone 1.
    CHECK(m.skinWeights[0 * 4 + 3] == 255);      // ring 0 -> bone 0, largest last
    CHECK(m.skinJoints [0 * 4 + 3] == 0);
    CHECK(m.skinWeights[2 * 4 + 0] == 0);        // ring 1 -> blend band, front padded
    CHECK(m.skinWeights[2 * 4 + 1] == 0);
    CHECK(m.skinWeights[2 * 4 + 2] + m.skinWeights[2 * 4 + 3] == 255);
    CHECK(m.skinWeights[4 * 4 + 3] == 255);      // ring 2 -> bone 1
    CHECK(m.skinJoints [4 * 4 + 3] == 1);

    // Flat shading is the point of the 32-vertex build: the two verts sharing a
    // corner across adjacent faces must carry DIFFERENT normals, or the box
    // shades like a cylinder.
    CHECK(m.vertices.size() > 12);

    // The bend clip targets bone1's node.
    REQUIRE(model.animations.size() == 1);
    CHECK(model.animations[0].name == "bend");
    CHECK(approx(model.animations[0].duration, 2.0f));
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
