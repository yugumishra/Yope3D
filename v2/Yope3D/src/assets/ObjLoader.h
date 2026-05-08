#pragma once
#include <string>
#include <vector>
#include "../math/Vec3.h"
#include "../world/RenderMesh.h"

// ---------------------------------------------------------------------------
// ObjLoader — Parse Wavefront OBJ files and return CPU-side mesh data
//
// Supports:
// - Positions (v), normals (vn), texture coordinates (vt)
// - Smooth and flat shading (s flag)
// - Triangles and quads (auto-triangulated with fan method)
// - MTL files: parses usemtl, loads .mtl for Kd (diffuse color) and map_Kd (texture)
// ---------------------------------------------------------------------------

struct MaterialData {
    math::Vec3  diffuseColor        = {1.0f, 1.0f, 1.0f};  // Kd from MTL
    std::string diffuseTexturePath;                          // map_Kd from MTL
};

struct LoadedMesh {
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
    MaterialData          material;
    std::string           name;      // from "o" or "g" directive
};

namespace ObjLoader {
    // Load an OBJ file from a full filesystem path.
    // Throws std::runtime_error on parse or file errors.
    LoadedMesh load(const std::string& absPath);
}
