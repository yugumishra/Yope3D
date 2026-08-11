#pragma once
#include <string>
#include <vector>
#include "../math/Vec3.h"
#include "../math/Vec4.h"
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

// PBR metallic-roughness material, fed by both the OBJ/MTL parser and GltfLoader.
// All *Path values are relative to YOPE_ASSETS_DIR (or a synthetic key registered
// in AssetManager for glTF-embedded images). hasMaterial gates whether the World
// factory attaches an ecs::Material; when false the mesh uses the legacy default.
struct MaterialData {
    bool        hasMaterial     = false;
    math::Vec4  albedoFactor    = {1.0f, 1.0f, 1.0f, 1.0f};  // Kd / baseColorFactor
    std::string albedoPath;                                   // map_Kd / baseColorTexture
    std::string normalPath;                                   // map_Kn|map_Bump / normalTexture
    std::string metalRoughPath;                               // metallicRoughnessTexture
    std::string occlusionPath;                                // occlusionTexture
    std::string emissivePath;                                 // map_Ke / emissiveTexture
    float       metallicFactor  = 0.0f;                       // OBJ default non-metal
    float       roughnessFactor = 1.0f;
    math::Vec3  emissiveFactor  = {0.0f, 0.0f, 0.0f};
    float       normalScale     = 1.0f;
};

struct LoadedMesh {
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
    MaterialData          material;
    std::string           name;      // from "o" or "g" directive

    // ---- Skinning (GltfLoader only; ObjLoader leaves these at their defaults) ----
    //
    // Self-describing width: influenceCount is 0 for an unskinned mesh, else 4 or
    // 8, chosen from what the heaviest vertex actually uses. skinJoints and
    // skinWeights are both influenceCount entries per vertex, parallel to
    // `vertices`. Joint values index the owning LoadedSkin::joints, NOT nodes.
    //
    // Two invariants the packer guarantees, both relied on downstream:
    //   * each vertex's weights sum to exactly 255 (not ~255)
    //   * the LARGEST weight is LAST, and unused slots are zero-padded at the
    //     FRONT — so a consumer may store only influenceCount-1 weights and
    //     derive the last as 255 - sum, which is exactly sum-preserving.
    uint8_t               influenceCount    = 0;
    std::vector<uint8_t>  skinJoints;
    std::vector<uint8_t>  skinWeights;
    // Vertices whose influences exceeded kMaxInfluences and were dropped. This
    // count predicts whether an asset will deform correctly far better than any
    // weight-precision metric — surface it, don't swallow it.
    uint32_t              truncatedVertices = 0;
};

namespace ObjLoader {
    // Load an OBJ file from a full filesystem path.
    // Throws std::runtime_error on parse or file errors.
    LoadedMesh load(const std::string& absPath);
}
