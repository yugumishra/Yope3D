// ---------------------------------------------------------------------------
// MeshBuild tests — the CPU vertex-finalisation pipeline every mesh passes
// through on its way to the GPU (tangent derivation + octahedral packing).
//
// This had no coverage before dynamic meshes existed, which was tolerable while
// it ran once per mesh at load. It now runs again on EVERY dynamic-mesh update
// (see the COST note on RenderMesh), so it is both the hot path and the thing
// most likely to regress silently: a wrong tangent or a non-deterministic pack
// shows up as subtly wrong shading, never as an error.
//
// Headless — MeshBuild.cpp pulls in Vulkan headers via RenderMesh.h but no
// Vulkan library, the same tier as yope_serialization_tests / yope_gltf_tests.
// ---------------------------------------------------------------------------
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "../src/rendering/MeshBuild.h"
#include "../src/world/RenderMesh.h"
#include "../src/math/OctEncode.h"
#include "../src/math/Vec3.h"
#include "../src/math/Vec4.h"

#include <cmath>
#include <cstring>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {

Vertex mkVertex(float px, float py, float pz,
                float nx, float ny, float nz,
                float u = 0.0f, float v = 0.0f) {
    Vertex vert{};
    vert.position[0] = px; vert.position[1] = py; vert.position[2] = pz;
    vert.normal[0]   = nx; vert.normal[1]   = ny; vert.normal[2]   = nz;
    vert.uv[0]       = u;  vert.uv[1]       = v;
    return vert;
}

// One +Z-facing triangle with a UV layout whose gradient runs along +X, so the
// derived tangent has an analytically known answer.
void unitTriangle(std::vector<Vertex>& verts, std::vector<uint32_t>& indices) {
    verts = {
        mkVertex(0.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f),
        mkVertex(1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f),
        mkVertex(0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f),
    };
    indices = {0, 1, 2};
}

float angleBetweenDeg(const math::Vec3& a, const math::Vec3& b) {
    float d = a.normalize().dot(b.normalize());
    d = d < -1.0f ? -1.0f : (d > 1.0f ? 1.0f : d);
    return std::acos(d) * 180.0f / 3.14159265358979323846f;
}

} // namespace

TEST_CASE("computeTangents derives the UV-aligned tangent", "[meshbuild][tangent]") {
    std::vector<Vertex>   verts;
    std::vector<uint32_t> indices;
    unitTriangle(verts, indices);

    const auto tangents = meshbuild::computeTangents(verts, indices);
    REQUIRE(tangents.size() == verts.size());

    // u increases along +X, so the tangent must be +X on every vertex, and it
    // must be unit length (Gram-Schmidt normalises).
    for (const auto& t : tangents) {
        REQUIRE_THAT(t.x, WithinAbs(1.0f, 1e-5f));
        REQUIRE_THAT(t.y, WithinAbs(0.0f, 1e-5f));
        REQUIRE_THAT(t.z, WithinAbs(0.0f, 1e-5f));
        REQUIRE_THAT(std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z), WithinAbs(1.0f, 1e-5f));
        REQUIRE(std::fabs(t.w) == 1.0f);      // handedness is strictly +-1
    }
}

TEST_CASE("computeTangents keeps the tangent perpendicular to the normal",
          "[meshbuild][tangent]") {
    // A skewed triangle whose UV gradient is deliberately NOT perpendicular to
    // the normal: Gram-Schmidt must still return an orthogonal frame, which is
    // what the shader's bitangent reconstruction assumes.
    std::vector<Vertex> verts = {
        mkVertex(0.0f, 0.0f, 0.0f,  0.0f, 0.577f, 0.816f,  0.0f, 0.0f),
        mkVertex(2.0f, 0.3f, 0.1f,  0.0f, 0.577f, 0.816f,  1.0f, 0.2f),
        mkVertex(0.4f, 1.7f, 0.9f,  0.0f, 0.577f, 0.816f,  0.3f, 1.0f),
    };
    std::vector<uint32_t> indices = {0, 1, 2};

    const auto tangents = meshbuild::computeTangents(verts, indices);
    for (size_t i = 0; i < verts.size(); ++i) {
        const math::Vec3 n{verts[i].normal[0], verts[i].normal[1], verts[i].normal[2]};
        const math::Vec3 t{tangents[i].x, tangents[i].y, tangents[i].z};
        REQUIRE_THAT(n.normalize().dot(t), WithinAbs(0.0f, 1e-5f));
        REQUIRE_THAT(t.length(), WithinAbs(1.0f, 1e-5f));
    }
}

TEST_CASE("computeTangents falls back to a perpendicular on degenerate UVs",
          "[meshbuild][tangent]") {
    // Every vertex at uv (0,0) — the case a dynamic mesh hits whenever the
    // caller omits UVs. The result must still be a usable orthonormal frame
    // rather than a zero or NaN tangent.
    std::vector<Vertex> verts = {
        mkVertex(0.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f),
        mkVertex(1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f),
        mkVertex(0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f),
    };
    std::vector<uint32_t> indices = {0, 1, 2};

    const auto tangents = meshbuild::computeTangents(verts, indices);
    for (size_t i = 0; i < verts.size(); ++i) {
        const math::Vec3 t{tangents[i].x, tangents[i].y, tangents[i].z};
        REQUIRE(std::isfinite(t.x));
        REQUIRE(std::isfinite(t.y));
        REQUIRE(std::isfinite(t.z));
        REQUIRE_THAT(t.length(), WithinAbs(1.0f, 1e-5f));
        const math::Vec3 n{verts[i].normal[0], verts[i].normal[1], verts[i].normal[2]};
        REQUIRE_THAT(n.dot(t), WithinAbs(0.0f, 1e-5f));
    }
}

TEST_CASE("packVertices preserves position and uv exactly", "[meshbuild][pack]") {
    // Position and uv stay float32 in PackedVertex, so they must survive the
    // pack bit-exact — only the normal and tangent are quantised.
    std::vector<Vertex>   verts;
    std::vector<uint32_t> indices;
    unitTriangle(verts, indices);
    verts[1].uv[0] = 0.37f;
    verts[1].uv[1] = 0.91f;

    const auto packed = meshbuild::buildPacked(verts, indices);
    REQUIRE(packed.size() == verts.size());
    for (size_t i = 0; i < verts.size(); ++i) {
        REQUIRE(packed[i].position[0] == verts[i].position[0]);
        REQUIRE(packed[i].position[1] == verts[i].position[1]);
        REQUIRE(packed[i].position[2] == verts[i].position[2]);
        REQUIRE(packed[i].uv[0] == verts[i].uv[0]);
        REQUIRE(packed[i].uv[1] == verts[i].uv[1]);
    }
}

TEST_CASE("packVertices round-trips normals within the oct16 error bound",
          "[meshbuild][pack]") {
    // The encoder's own bound is ~0.028 deg worst case (see math_tests'
    // octahedral round-trip). Assert the whole pipeline stays under a loose
    // multiple of it, so a regression in normalisation or encoding is caught
    // without making this test brittle to the quantiser's exact error.
    std::vector<Vertex>   verts;
    std::vector<uint32_t> indices;
    float maxErrDeg = 0.0f;

    for (int i = 0; i < 64; ++i) {
        const float theta = 0.11f * static_cast<float>(i);
        const float phi   = 0.07f * static_cast<float>(i);
        const math::Vec3 n = math::Vec3{std::sin(theta) * std::cos(phi),
                                        std::sin(theta) * std::sin(phi),
                                        std::cos(theta)}.normalize();
        verts.push_back(mkVertex(static_cast<float>(i), 0.0f, 0.0f,
                                  n.x, n.y, n.z,
                                  static_cast<float>(i) * 0.1f, 0.0f));
    }
    for (uint32_t i = 0; i + 2 < verts.size(); i += 3)
        indices.insert(indices.end(), {i, i + 1, i + 2});

    const auto packed = meshbuild::buildPacked(verts, indices);
    for (size_t i = 0; i < verts.size(); ++i) {
        const math::Vec3 original{verts[i].normal[0], verts[i].normal[1], verts[i].normal[2]};
        const math::Vec3 decoded = math::octDecodeSnorm16(packed[i].normalOct);
        maxErrDeg = std::max(maxErrDeg, angleBetweenDeg(original, decoded));
    }
    INFO("worst-case normal error through buildPacked (deg): " << maxErrDeg);
    REQUIRE(maxErrDeg < 0.1f);
}

TEST_CASE("buildPacked is deterministic across repeated calls",
          "[meshbuild][pack][dynamic]") {
    // The regression guard that matters most for dynamic meshes: a dynamic mesh
    // repacks its whole array every update, so identical input MUST produce
    // byte-identical output. If it ever did not, an unchanged surface would
    // shimmer between frames with nothing in the scene having moved.
    std::vector<Vertex>   verts;
    std::vector<uint32_t> indices;
    unitTriangle(verts, indices);

    const auto a = meshbuild::buildPacked(verts, indices);
    const auto b = meshbuild::buildPacked(verts, indices);
    REQUIRE(a.size() == b.size());
    REQUIRE(std::memcmp(a.data(), b.data(), a.size() * sizeof(PackedVertex)) == 0);
}

TEST_CASE("buildPacked handles empty and index-free input", "[meshbuild][pack]") {
    // A dynamic mesh legitimately stages an empty frame (a slice mode that
    // culls everything, say), so this must not read out of bounds or crash.
    REQUIRE(meshbuild::buildPacked({}, {}).empty());

    std::vector<Vertex> verts = {
        mkVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f),
        mkVertex(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f),
        mkVertex(0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f),
    };
    // Vertices with no triangles referencing them: no UV gradient exists, so
    // every tangent takes the degenerate fallback rather than staying zero.
    const auto packed = meshbuild::buildPacked(verts, {});
    REQUIRE(packed.size() == 3);
    for (const auto& p : packed) {
        const math::Vec3 t = math::octDecodeSnorm16(p.tangentOct);
        REQUIRE_THAT(t.length(), WithinAbs(1.0f, 1e-3f));
    }
}

TEST_CASE("validateGeometry rejects over-capacity arrays", "[meshbuild][dynamic]") {
    // Capacity on a dynamic mesh is fixed at creation and never grows, so an
    // oversized update must be refused rather than silently truncated — a
    // truncated frame is a torn mix of old and new geometry.
    const std::vector<uint32_t> tri = {0, 1, 2};

    REQUIRE(meshbuild::validateGeometry(3, tri, 3, 3));       // exactly at capacity
    REQUIRE_FALSE(meshbuild::validateGeometry(4, tri, 3, 3)); // one vertex over
    REQUIRE_FALSE(meshbuild::validateGeometry(3, tri, 3, 2)); // one index over
    REQUIRE(meshbuild::validateGeometry(0, {}, 3, 3));        // empty frame is legal
}

TEST_CASE("validateGeometry rejects out-of-range indices", "[meshbuild][dynamic]") {
    // The check that actually protects the GPU: a dynamic mesh is refilled from
    // arrays the engine did not produce, and an index past the vertex count is a
    // read off the end of the vertex buffer.
    REQUIRE(meshbuild::validateGeometry(3, {0, 1, 2}, 16, 16));
    REQUIRE_FALSE(meshbuild::validateGeometry(3, {0, 1, 3}, 16, 16));  // == count
    REQUIRE_FALSE(meshbuild::validateGeometry(3, {0, 1, 99}, 16, 16)); // far past
    REQUIRE_FALSE(meshbuild::validateGeometry(0, {0}, 16, 16));        // any index vs. no verts

    // Capacity alone must not be mistaken for validity: room for the indices
    // says nothing about whether they point at real vertices.
    REQUIRE_FALSE(meshbuild::validateGeometry(2, {0, 1, 2}, 16, 16));
}

TEST_CASE("PackedVertex stays 32 bytes with the documented offsets",
          "[meshbuild][layout]") {
    // The GPU vertex-input attributes hard-code these offsets (Renderer's
    // pipeline setup), and skin.comp reads the same struct as an SSBO. A silent
    // layout change here would misread every vertex on the GPU.
    REQUIRE(sizeof(PackedVertex) == 32);
    REQUIRE(offsetof(PackedVertex, position)   == 0);
    REQUIRE(offsetof(PackedVertex, uv)         == 12);
    REQUIRE(offsetof(PackedVertex, normalOct)  == 20);
    REQUIRE(offsetof(PackedVertex, tangentOct) == 24);
    REQUIRE(offsetof(PackedVertex, handedness) == 28);
}
