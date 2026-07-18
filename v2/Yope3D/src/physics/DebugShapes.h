#pragma once
#include "../world/RenderMesh.h"
#include <vector>
#include <utility>
#include <cmath>

// Procedural unit-geometry for physics debug rendering.
// Box:    half-extent = 1 on all axes — scale by hull.getBroadExtent() to fit.
// Sphere: radius = 1                  — scale uniformly by sphere radius to fit.
namespace DebugShapes {

inline std::pair<std::vector<Vertex>, std::vector<uint32_t>> makeBox() {
    // 6 faces × 4 vertices = 24 vertices, 6 faces × 2 tris = 12 tris
    struct Face { float nx, ny, nz; float verts[4][3]; };
    constexpr Face faces[6] = {
        { 1, 0, 0, {{ 1,-1,-1},{ 1, 1,-1},{ 1, 1, 1},{ 1,-1, 1}}},
        {-1, 0, 0, {{-1,-1, 1},{-1, 1, 1},{-1, 1,-1},{-1,-1,-1}}},
        { 0, 1, 0, {{-1, 1,-1},{ 1, 1,-1},{ 1, 1, 1},{-1, 1, 1}}},
        { 0,-1, 0, {{-1,-1, 1},{ 1,-1, 1},{ 1,-1,-1},{-1,-1,-1}}},
        { 0, 0, 1, {{-1,-1, 1},{ 1,-1, 1},{ 1, 1, 1},{-1, 1, 1}}},
        { 0, 0,-1, {{ 1,-1,-1},{-1,-1,-1},{-1, 1,-1},{ 1, 1,-1}}},
    };

    std::vector<Vertex>   verts;
    std::vector<uint32_t> idx;
    verts.reserve(24);
    idx.reserve(36);

    float uvs[4][2] = {{0,0},{1,0},{1,1},{0,1}};
    for (auto& f : faces) {
        uint32_t base = (uint32_t)verts.size();
        for (int v = 0; v < 4; ++v) {
            Vertex vtx{};
            vtx.position[0] = f.verts[v][0];
            vtx.position[1] = f.verts[v][1];
            vtx.position[2] = f.verts[v][2];
            vtx.normal[0] = f.nx; vtx.normal[1] = f.ny; vtx.normal[2] = f.nz;
            vtx.uv[0] = uvs[v][0]; vtx.uv[1] = uvs[v][1];
            verts.push_back(vtx);
        }
        idx.insert(idx.end(), {base,base+1,base+2, base,base+2,base+3});
    }
    return {verts, idx};
}

inline std::pair<std::vector<Vertex>, std::vector<uint32_t>> makeSphere(int rings = 12, int segs = 16) {
    constexpr float PI = 3.14159265359f;
    std::vector<Vertex>   verts;
    std::vector<uint32_t> idx;
    verts.reserve((rings + 1) * (segs + 1));

    for (int r = 0; r <= rings; ++r) {
        float theta = PI * (float)r / rings;
        float st = std::sin(theta), ct = std::cos(theta);
        for (int s = 0; s <= segs; ++s) {
            float phi = 2.0f * PI * (float)s / segs;
            float sp = std::sin(phi), cp = std::cos(phi);
            Vertex v{};
            v.position[0] = st * cp;
            v.position[1] = ct;
            v.position[2] = st * sp;
            v.normal[0] = v.position[0];
            v.normal[1] = v.position[1];
            v.normal[2] = v.position[2];
            v.uv[0] = (float)s / segs;
            v.uv[1] = (float)r / rings;
            verts.push_back(v);
        }
    }

    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segs; ++s) {
            uint32_t a = (uint32_t)(r * (segs + 1) + s);
            uint32_t b = a + (uint32_t)(segs + 1);
            idx.insert(idx.end(), {a, b, a+1, a+1, b, b+1});
        }
    }
    return {verts, idx};
}

// Capsule debug mesh: dims baked in. Use T*R model matrix (no scale).
inline std::pair<std::vector<Vertex>, std::vector<uint32_t>>
makeCapsule(float radius, float halfHeight, int rings = 6, int segs = 12) {
    constexpr float PI = 3.14159265359f;
    std::vector<Vertex>   verts;
    std::vector<uint32_t> idx;

    auto pushRing = [&](float y, float r_xz, float ny) {
        for (int s = 0; s <= segs; ++s) {
            float phi = 2.0f * PI * (float)s / segs;
            float cp = std::cos(phi), sp = std::sin(phi);
            Vertex v{};
            v.position[0] = r_xz * cp; v.position[1] = y; v.position[2] = r_xz * sp;
            float cosT = (radius > 0.f) ? r_xz / radius : 0.f;
            v.normal[0] = cosT * cp; v.normal[1] = ny; v.normal[2] = cosT * sp;
            v.uv[0] = (float)s / segs; v.uv[1] = 0.f;
            verts.push_back(v);
        }
    };

    // Bottom hemisphere rings (south pole to equator)
    for (int r = 0; r <= rings; ++r) {
        float theta = -PI * 0.5f + PI * 0.5f * (float)r / rings;
        pushRing(-halfHeight + radius * std::sin(theta), radius * std::cos(theta), std::sin(theta));
    }
    // Top hemisphere rings (equator to north pole)
    for (int r = 0; r <= rings; ++r) {
        float theta = PI * 0.5f * (float)r / rings;
        pushRing(halfHeight + radius * std::sin(theta), radius * std::cos(theta), std::sin(theta));
    }

    int perRow    = segs + 1;
    int totalRows = 2 * (rings + 1);
    for (int r = 0; r < totalRows - 1; ++r) {
        for (int s = 0; s < segs; ++s) {
            uint32_t a = (uint32_t)(r * perRow + s);
            uint32_t b = a + (uint32_t)perRow;
            idx.insert(idx.end(), {a, b, a+1, a+1, b, b+1});
        }
    }
    return {verts, idx};
}

// Cylinder debug mesh: dims baked in. Use T*R model matrix (no scale).
inline std::pair<std::vector<Vertex>, std::vector<uint32_t>>
makeCylinder(float radius, float halfHeight, int segs = 12) {
    constexpr float PI = 3.14159265359f;
    std::vector<Vertex>   verts;
    std::vector<uint32_t> idx;

    // Side wall (two rings)
    for (int ring = 0; ring < 2; ++ring) {
        float y = (ring == 0) ? -halfHeight : halfHeight;
        uint32_t base = (uint32_t)verts.size();
        for (int s = 0; s <= segs; ++s) {
            float phi = 2.0f * PI * (float)s / segs;
            float cp = std::cos(phi), sp = std::sin(phi);
            Vertex v{};
            v.position[0] = radius * cp; v.position[1] = y; v.position[2] = radius * sp;
            v.normal[0] = cp; v.normal[1] = 0.f; v.normal[2] = sp;
            v.uv[0] = (float)s / segs; v.uv[1] = (float)ring;
            verts.push_back(v);
        }
    }
    uint32_t perRing = (uint32_t)(segs + 1);
    for (int s = 0; s < segs; ++s) {
        uint32_t a = (uint32_t)s, b = a + perRing;
        idx.insert(idx.end(), {a, b, a+1, a+1, b, b+1});
    }

    // Caps (triangle fans)
    for (int cap = 0; cap < 2; ++cap) {
        float y  = (cap == 0) ? -halfHeight : halfHeight;
        float ny = (cap == 0) ? -1.f : 1.f;
        uint32_t centerIdx = (uint32_t)verts.size();
        Vertex cv{}; cv.position[1] = y; cv.normal[1] = ny; cv.uv[0] = 0.5f; cv.uv[1] = 0.5f;
        verts.push_back(cv);
        uint32_t ringStart = (uint32_t)verts.size();
        for (int s = 0; s <= segs; ++s) {
            float phi = 2.0f * PI * (float)s / segs;
            float cp = std::cos(phi), sp = std::sin(phi);
            Vertex v{}; v.position[0] = radius * cp; v.position[1] = y; v.position[2] = radius * sp;
            v.normal[1] = ny; v.uv[0] = 0.5f + 0.5f*cp; v.uv[1] = 0.5f + 0.5f*sp;
            verts.push_back(v);
        }
        for (int s = 0; s < segs; ++s) {
            if (cap == 0) idx.insert(idx.end(), {centerIdx, ringStart+(uint32_t)s+1, ringStart+(uint32_t)s});
            else          idx.insert(idx.end(), {centerIdx, ringStart+(uint32_t)s,   ringStart+(uint32_t)s+1});
        }
    }
    return {verts, idx};
}

} // namespace DebugShapes
