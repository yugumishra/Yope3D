#include "Primitives.h"
#include <cmath>
#include <map>
#include <set>

LoadedMesh Primitives::cube() {
    LoadedMesh mesh;
    mesh.name = "Cube";

    // 24 vertices (6 faces × 4 corners, with per-face normals and UV seams).
    // This matches the original hardcoded kDefaultVertices from Engine.cpp.
    const Vertex vertices[] = {
        // Front face (z = 1, normal = +Z)
        {{-1.0f, -1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, {0.0f, 1.0f}},
        {{ 1.0f,  1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, {1.0f, 0.0f}},
        {{-1.0f,  1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, {0.0f, 0.0f}},
        {{ 1.0f, -1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, {1.0f, 1.0f}},

        // Back face (z = -1, normal = -Z)
        {{-1.0f, -1.0f, -1.0f}, { 0.0f,  0.0f, -1.0f}, {0.0f, 1.0f}},
        {{ 1.0f,  1.0f, -1.0f}, { 0.0f,  0.0f, -1.0f}, {1.0f, 0.0f}},
        {{-1.0f,  1.0f, -1.0f}, { 0.0f,  0.0f, -1.0f}, {0.0f, 0.0f}},
        {{ 1.0f, -1.0f, -1.0f}, { 0.0f,  0.0f, -1.0f}, {1.0f, 1.0f}},

        // Top face (y = 1, normal = +Y)
        {{-1.0f,  1.0f, -1.0f}, { 0.0f,  1.0f,  0.0f}, {0.0f, 1.0f}},
        {{ 1.0f,  1.0f,  1.0f}, { 0.0f,  1.0f,  0.0f}, {1.0f, 0.0f}},
        {{-1.0f,  1.0f,  1.0f}, { 0.0f,  1.0f,  0.0f}, {0.0f, 0.0f}},
        {{ 1.0f,  1.0f, -1.0f}, { 0.0f,  1.0f,  0.0f}, {1.0f, 1.0f}},

        // Bottom face (y = -1, normal = -Y)
        {{-1.0f, -1.0f, -1.0f}, { 0.0f, -1.0f,  0.0f}, {0.0f, 1.0f}},
        {{ 1.0f, -1.0f,  1.0f}, { 0.0f, -1.0f,  0.0f}, {1.0f, 0.0f}},
        {{-1.0f, -1.0f,  1.0f}, { 0.0f, -1.0f,  0.0f}, {0.0f, 0.0f}},
        {{ 1.0f, -1.0f, -1.0f}, { 0.0f, -1.0f,  0.0f}, {1.0f, 1.0f}},

        // Right face (x = 1, normal = +X)
        {{ 1.0f, -1.0f, -1.0f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 1.0f}},
        {{ 1.0f,  1.0f,  1.0f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 0.0f}},
        {{ 1.0f, -1.0f,  1.0f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 0.0f}},
        {{ 1.0f,  1.0f, -1.0f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 1.0f}},

        // Left face (x = -1, normal = -X)
        {{-1.0f, -1.0f, -1.0f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 1.0f}},
        {{-1.0f,  1.0f,  1.0f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 0.0f}},
        {{-1.0f, -1.0f,  1.0f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 0.0f}},
        {{-1.0f,  1.0f, -1.0f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 1.0f}},
    };

    // 36 indices (6 faces × 2 triangles per face × 3 vertices per triangle).
    const uint32_t indices[] = {
        0, 1, 2, 0, 3, 1,          // Front
        4, 5, 6, 4, 7, 5,          // Back
        8, 9, 10, 8, 11, 9,        // Top
        12, 13, 14, 12, 15, 13,    // Bottom
        16, 17, 18, 16, 19, 17,    // Right
        20, 21, 22, 20, 23, 21,    // Left
    };

    mesh.vertices.insert(mesh.vertices.end(), vertices, vertices + 24);
    mesh.indices.insert(mesh.indices.end(), indices, indices + 36);

    return mesh;
}

LoadedMesh Primitives::icosphere(float radius, int subdivisions) {
    LoadedMesh mesh;
    mesh.name = "Icosphere";

    // Base icosahedron: 12 vertices (unit radius, already normalized)
    const float ico_vertices[][3] = {
        {  0.000000f, -1.000000f,  0.000000f },  // 0
        {  0.723600f, -0.447215f,  0.525720f },  // 1
        { -0.276385f, -0.447215f,  0.850640f },  // 2
        { -0.894425f, -0.447215f,  0.000000f },  // 3
        { -0.276385f, -0.447215f, -0.850640f },  // 4
        {  0.723600f, -0.447215f, -0.525720f },  // 5
        {  0.276385f,  0.447215f,  0.850640f },  // 6
        { -0.723600f,  0.447215f,  0.525720f },  // 7
        { -0.723600f,  0.447215f, -0.525720f },  // 8
        {  0.276385f,  0.447215f, -0.850640f },  // 9
        {  0.894425f,  0.447215f,  0.000000f },  // 10
        {  0.000000f,  1.000000f,  0.000000f },  // 11
    };

    // Spherical UV mapping from normal vector (Carles Araguz, CC BY-SA 3.0)
    // Avoids harsh seam artifacts by using proper spherical coordinates
    auto getTexCoord = [](float nx, float ny, float nz) {
        const float PI = 3.14159265359f;
        float theta = (std::atan2(nx, nz) / PI) / 2.0f + 1.0f;
        float phi = (std::asin(-ny) / (PI / 2.0f)) / 2.0f + 1.0f;
        return std::make_pair(theta, phi);
    };

    // Base icosahedron: 20 triangles (0-based indices)
    const uint32_t ico_indices[] = {
        0, 1, 2,    0, 2, 3,    0, 3, 4,    0, 4, 5,    0, 5, 1,
        1, 5, 10,   2, 1, 6,    3, 2, 7,    4, 3, 8,    5, 4, 9,
        1, 10, 6,   2, 6, 7,    3, 7, 8,    4, 8, 9,    5, 9, 10,
        6, 10, 11,  7, 6, 11,   8, 7, 11,   9, 8, 11,   10, 9, 11,
    };

    // Initialize mesh with base icosahedron.
    mesh.vertices.resize(12);
    for (int i = 0; i < 12; ++i) {
        mesh.vertices[i].position[0] = ico_vertices[i][0];
        mesh.vertices[i].position[1] = ico_vertices[i][1];
        mesh.vertices[i].position[2] = ico_vertices[i][2];
        // Normal = position (since it's already normalized for unit sphere)
        mesh.vertices[i].normal[0] = ico_vertices[i][0];
        mesh.vertices[i].normal[1] = ico_vertices[i][1];
        mesh.vertices[i].normal[2] = ico_vertices[i][2];
        // UV from spherical coordinate mapping (eliminates seam artifacts)
        auto uv = getTexCoord(ico_vertices[i][0], ico_vertices[i][1], ico_vertices[i][2]);
        mesh.vertices[i].uv[0] = uv.first;
        mesh.vertices[i].uv[1] = uv.second;
    }

    mesh.indices.insert(mesh.indices.end(), ico_indices, ico_indices + 60);  // 20 triangles * 3 indices

    // Subdivide the mesh.
    for (int subdiv = 0; subdiv < subdivisions; ++subdiv) {
        std::vector<Vertex> newVertices = mesh.vertices;
        std::vector<uint32_t> newIndices;

        // Map from edge (min, max vertex pair) to midpoint index.
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> edgeToMidpoint;

        // Process each triangle.
        for (size_t i = 0; i < mesh.indices.size(); i += 3) {
            uint32_t i0 = mesh.indices[i];
            uint32_t i1 = mesh.indices[i + 1];
            uint32_t i2 = mesh.indices[i + 2];

            // Get or create midpoint vertices for each edge.
            auto getMidpoint = [&](uint32_t a, uint32_t b) -> uint32_t {
                uint32_t minIdx = std::min(a, b);
                uint32_t maxIdx = std::max(a, b);
                auto key = std::make_pair(minIdx, maxIdx);

                auto it = edgeToMidpoint.find(key);
                if (it != edgeToMidpoint.end())
                    return it->second;

                // Create new midpoint vertex.
                const Vertex& va = mesh.vertices[a];
                const Vertex& vb = mesh.vertices[b];

                Vertex midpoint;
                midpoint.position[0] = (va.position[0] + vb.position[0]) * 1.0f;
                midpoint.position[1] = (va.position[1] + vb.position[1]) * 1.0f;
                midpoint.position[2] = (va.position[2] + vb.position[2]) * 1.0f;

                // Normalize to project onto unit sphere.
                float len = std::sqrt(midpoint.position[0] * midpoint.position[0] +
                                     midpoint.position[1] * midpoint.position[1] +
                                     midpoint.position[2] * midpoint.position[2]);
                if (len > 0.0f) {
                    midpoint.position[0] /= len;
                    midpoint.position[1] /= len;
                    midpoint.position[2] /= len;
                }

                // Normal = position (for unit sphere).
                midpoint.normal[0] = midpoint.position[0];
                midpoint.normal[1] = midpoint.position[1];
                midpoint.normal[2] = midpoint.position[2];

                // UV from spherical coordinate mapping (consistent with base vertices)
                auto uv = getTexCoord(midpoint.position[0], midpoint.position[1], midpoint.position[2]);
                midpoint.uv[0] = uv.first;
                midpoint.uv[1] = uv.second;

                uint32_t midIdx = newVertices.size();
                newVertices.push_back(midpoint);
                edgeToMidpoint[key] = midIdx;
                return midIdx;
            };

            // Get midpoints for the three edges.
            uint32_t m01 = getMidpoint(i0, i1);
            uint32_t m12 = getMidpoint(i1, i2);
            uint32_t m20 = getMidpoint(i2, i0);

            // Create four triangles from the original triangle.
            // Triangle 1: original corners
            newIndices.push_back(i0);
            newIndices.push_back(m01);
            newIndices.push_back(m20);

            // Triangle 2: edge midpoints and middle corner
            newIndices.push_back(m01);
            newIndices.push_back(i1);
            newIndices.push_back(m12);

            // Triangle 3: more edge midpoints and corners
            newIndices.push_back(m20);
            newIndices.push_back(m12);
            newIndices.push_back(i2);

            // Triangle 4: center (new triangle)
            newIndices.push_back(m01);
            newIndices.push_back(m12);
            newIndices.push_back(m20);
        }

        mesh.vertices = newVertices;
        mesh.indices = newIndices;
    }

    // Apply radius scaling.
    for (auto& vert : mesh.vertices) {
        vert.position[0] *= radius;
        vert.position[1] *= radius;
        vert.position[2] *= radius;
        // Normals stay unit length (pointing outward).
    }

    // After all vertices and indices are created:
    // After spherical UV mapping, detect and fix seam triangles
  std::vector<bool> isSeamTriangle;
  std::set<uint32_t> seamVertices;

  // First pass: identify seam-straddling triangles and their vertices
  for (size_t i = 0; i < mesh.indices.size(); i += 3) {
      uint32_t i0 = mesh.indices[i];
      uint32_t i1 = mesh.indices[i + 1];
      uint32_t i2 = mesh.indices[i + 2];

      float u_max = std::max({mesh.vertices[i0].uv[0], mesh.vertices[i1].uv[0], mesh.vertices[i2].uv[0]});
      float u_min = std::min({mesh.vertices[i0].uv[0], mesh.vertices[i1].uv[0], mesh.vertices[i2].uv[0]});

      bool straddles = (u_max - u_min > 1.0f);
      isSeamTriangle.push_back(straddles);

      if (straddles) {
          seamVertices.insert(i0);
          seamVertices.insert(i1);
          seamVertices.insert(i2);
      }
  }

  // Second pass: duplicate low-U vertices that appear in both seam and non-seam triangles
  std::map<uint32_t, uint32_t> duplicates;  // original → new duplicate index

  for (size_t i = 0; i < mesh.indices.size(); i += 3) {
      if (!isSeamTriangle[i / 3]) continue;

      for (int j = 0; j < 3; ++j) {
          uint32_t vertIdx = mesh.indices[i + j];
          float u = mesh.vertices[vertIdx].uv[0];

          if (u < 1.0f) {
              // Check if this vertex is also used by non-seam triangles
              bool usedByNonSeam = false;
              for (size_t k = 0; k < mesh.indices.size(); k += 3) {
                  if (!isSeamTriangle[k / 3]) {
                      if (mesh.indices[k] == vertIdx || mesh.indices[k+1] == vertIdx || mesh.indices[k+2] == vertIdx) {
                          usedByNonSeam = true;
                          break;
                      }
                  }
              }

              if (usedByNonSeam && duplicates.find(vertIdx) == duplicates.end()) {
                  // Create duplicate with adjusted UV
                  Vertex dup = mesh.vertices[vertIdx];
                  dup.uv[0] += 1.0f;
                  uint32_t dupIdx = mesh.vertices.size();
                  mesh.vertices.push_back(dup);
                  duplicates[vertIdx] = dupIdx;
              }

              if (duplicates.find(vertIdx) != duplicates.end()) {
                  mesh.indices[i + j] = duplicates[vertIdx];
              }
          }
      }
  }

    return mesh;
}

LoadedMesh Primitives::rect(const math::Vec3& extents) {
    LoadedMesh mesh = cube();
    mesh.name = "Rect";

    // Scale all vertices by the extents (apply non-uniform scaling).
    for (auto& vert : mesh.vertices) {
        vert.position[0] *= extents.x;
        vert.position[1] *= extents.y;
        vert.position[2] *= extents.z;
    }

    return mesh;
}

LoadedMesh Primitives::plane(float halfExtent) {
    LoadedMesh mesh;
    mesh.name = "Plane";

    // XZ plane at y = -1, facing up (normal = +Y).
    // Four corners: (±halfExtent, -1, ±halfExtent)
    const Vertex vertices[] = {
        {{-halfExtent, -1.0f, -halfExtent}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ halfExtent, -1.0f,  halfExtent}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-halfExtent, -1.0f,  halfExtent}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
        {{ halfExtent, -1.0f, -halfExtent}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
    };

    const uint32_t indices[] = {
        0, 1, 2, 0, 3, 1,
    };

    mesh.vertices.insert(mesh.vertices.end(), vertices, vertices + 4);
    mesh.indices.insert(mesh.indices.end(), indices, indices + 6);

    return mesh;
}

LoadedMesh Primitives::sphere(float radius, int rings, int sectors) {
    LoadedMesh mesh;
    mesh.name = "Sphere";

    // Generate UV sphere: rings for latitude (0 = south pole, rings = north pole),
    // sectors for longitude (0 to 2π).
    const float PI = 3.14159265359f;
    const float invRings = 1.0f / rings;
    const float invSectors = 1.0f / sectors;

    for (int ring = 0; ring <= rings; ++ring) {
        const float phi = PI * ring * invRings;  // latitude angle [0, π]
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);

        for (int sector = 0; sector <= sectors; ++sector) {
            const float theta = 2.0f * PI * sector * invSectors;  // longitude angle [0, 2π]
            const float sinTheta = std::sin(theta);
            const float cosTheta = std::cos(theta);

            // Position: spherical coordinates
            float x = cosTheta * sinPhi;
            float y = cosPhi;
            float z = sinTheta * sinPhi;

            Vertex v{};
            v.position[0] = radius * x;
            v.position[1] = radius * y;
            v.position[2] = radius * z;

            // Normal: points outward (same as position for unit sphere at origin).
            v.normal[0] = x;
            v.normal[1] = y;
            v.normal[2] = z;

            // UV: sector/ring mapping
            v.uv[0] = static_cast<float>(sector) * invSectors;
            v.uv[1] = static_cast<float>(ring) * invRings;

            mesh.vertices.push_back(v);
        }
    }

    // Generate indices: connect quads as two triangles.
    for (int ring = 0; ring < rings; ++ring) {
        for (int sector = 0; sector < sectors; ++sector) {
            uint32_t a = ring * (sectors + 1) + sector;
            uint32_t b = a + sectors + 1;
            uint32_t c = a + 1;
            uint32_t d = b + 1;

            // First triangle (a, b, c)
            mesh.indices.push_back(a);
            mesh.indices.push_back(b);
            mesh.indices.push_back(c);

            // Second triangle (c, b, d)
            mesh.indices.push_back(c);
            mesh.indices.push_back(b);
            mesh.indices.push_back(d);
        }
    }

    return mesh;
}
