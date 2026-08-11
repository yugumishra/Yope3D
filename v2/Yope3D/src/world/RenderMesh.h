#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include "../gpu/Buffer.h"
#include "../math/Mat4.h"
#include "../math/Vec3.h"

class GpuDevice;

// Tracks how the mesh was created so the raytracer can choose the optimal
// intersection representation instead of always falling back to triangle soup.
enum class PrimitiveType {
    Custom,    // arbitrary mesh — triangle soup fallback
    Sphere,    // UV sphere — parametric; primitiveExtents.x = radius
    Icosphere, // icosphere  — parametric; primitiveExtents.x = radius
    Rect,      // rect(extents) — 6 quads; primitiveExtents = halfExtents
    Cube,      // unit cube     — 6 quads; primitiveExtents = {1,1,1}
    Plane,     // XZ plane      — 1 quad;  primitiveExtents.x = halfExtent
    Capsule,   // capsule (+Y axis, baked dims); primitiveExtents = {radius, halfHeight, 0}
    Cylinder,  // cylinder (+Y axis, baked dims); primitiveExtents = {radius, halfHeight, 0}
};

// ---------------------------------------------------------------------------
// Vertex
//
// 8-float layout matching the Java engine's rasterisation pipeline:
//   location 0 — position (xyz)
//   location 1 — normal   (xyz)
//   location 2 — uv       (st)
// ---------------------------------------------------------------------------

struct Vertex {
    float position[3];
    float normal[3];
    float uv[2];
};

// ---------------------------------------------------------------------------
// PackedVertex
//
// GPU-side upload format. Identical information to a Vertex (+ a tangent frame
// for normal mapping) compressed to exactly 32 bytes — half a cache line —
// via octahedral snorm16 encoding of the normal and tangent. The CPU keeps the
// float `Vertex` (RenderMesh::cpuVertices) as the authoring/working copy; only
// the GPU buffer stores PackedVertex. See Milestone 15 plan for the derivation.
//
//   location 0 — position    (xyz, float32)        offset  0
//   location 1 — uv          (st,  float32)        offset 12
//   location 2 — normalOct   (octahedral snorm16)  offset 20
//   location 3 — tangentOct  (octahedral snorm16)  offset 24
//   location 4 — handedness  (+-1, float32)        offset 28
// ---------------------------------------------------------------------------

struct PackedVertex {
    float   position[3];   // offset  0
    float   uv[2];         // offset 12
    int16_t normalOct[2];  // offset 20  (VK_FORMAT_R16G16_SNORM)
    int16_t tangentOct[2]; // offset 24  (VK_FORMAT_R16G16_SNORM)
    float   handedness;    // offset 28  (bitangent sign, +-1)
};
static_assert(sizeof(PackedVertex) == 32, "PackedVertex must stay 32 bytes");

// ---------------------------------------------------------------------------
// RenderMesh
//
// GPU-side representation of a mesh: owns a vertex Buffer and an index Buffer.
// Constructed by uploading CPU data through a staging buffer.
// Call destroy() before destroying the GpuDevice.
// ---------------------------------------------------------------------------

class Texture;

class RenderMesh {
public:
    RenderMesh(GpuDevice& gpu, VkCommandPool commandPool,
               const std::vector<Vertex>&   vertices,
               const std::vector<uint32_t>& indices);

    // Deferred-upload variant: records vertex + index buffer copies into
    // batch.cmd instead of doing a blocking per-buffer submit. Used by the async
    // scene-load commit pump to batch many mesh uploads into one fenced submit.
    RenderMesh(GpuDevice& gpu, BufferUploadBatch& batch,
               const std::vector<Vertex>&   vertices,
               const std::vector<uint32_t>& indices);

    void destroy(VkDevice device);

    // Bind vertex + index buffers and issue vkCmdDrawIndexed.
    void draw(VkCommandBuffer cmd) const;

    // Rendering properties (set during initialization or per-frame updates)
    Texture*    texture = nullptr;  // Non-owning pointer; nullptr = use default white texture
    float       color[3] = {1.0f, 1.0f, 1.0f};  // Solid color or texture modulation
    int         state = 0;  // Render state: STATE_SOLID (0) or STATE_TEXTURED (1)
    math::Mat4  modelMatrix;  // Updated each frame by physics hull sync
    bool        transformReady = false;  // False until first snapshot propagates; suppresses the 0,0,0 flicker
    bool        visible = true;          // Renderer skips this mesh when false (script-toggled hide)
    float       reflectivity = 0.0f;    // For raytracer: [0,1] mirror reflectance; 0 = fully diffuse

    // Raytracer metadata — set by World after mesh creation when the source is a known Primitive.
    PrimitiveType primitiveType    = PrimitiveType::Custom;
    math::Vec3    primitiveExtents = {1.0f, 1.0f, 1.0f};

    // CPU-side copy of vertex/index data. Retained for Custom triangle-soup packing.
    // Freed (clear + shrink_to_fit) for all other types once primitiveType is determined.
    std::vector<Vertex>   cpuVertices;
    std::vector<uint32_t> cpuIndices;

    // Absolute path to the source .obj file, if this mesh was drag-dropped from disk.
    // Empty for procedural/primitive meshes. Used for reference-based serialization.
    std::string sourcePath;

    // ---- Skinning (M16) ----
    //
    // A skinned mesh carries two extra buffers: `skinBuffer` (per-vertex joint
    // indices + weights, read as an SSBO by skin.comp) and `skinnedVertexBuffer`
    // (the compute pass's output, in the SAME 32-byte PackedVertex layout as the
    // source). draw() binds the output when present, which is the whole reason
    // this design keeps the main/shadow/picking passes untouched — they cannot
    // tell a pre-skinned mesh from a static one.
    //
    // The output buffer is per-mesh and meshes are never shared between entities
    // (World::attachMesh allocates a fresh RenderMesh each time), so per-mesh is
    // already per-instance.
    //
    // `skinInstance` indexes World's SkinInstance pool, which supplies the joint
    // palette. -1 until World::attachSkin wires it up.
    Buffer          skinBuffer;
    Buffer          skinnedVertexBuffer;
    int             skinInstance    = -1;
    uint8_t         influenceCount  = 0;   // 0 = unskinned; else 4 or 8

    // CPU-side copy of the influences, retained for the same reason cpuVertices
    // is: the scene serializer has to write them to the .ymesh sidecar, and the
    // GPU buffer is device-local and unreadable. influenceCount entries per
    // vertex each, parallel to cpuVertices.
    std::vector<uint8_t> cpuSkinJoints;
    std::vector<uint8_t> cpuSkinWeights;

    // Compute descriptor set for this mesh's src/skin/dst bindings. Static once
    // written (the three buffers never change), so it is allocated once by the
    // Renderer rather than per frame. The pool handle is retained non-owning
    // purely so destroy() can hand the set back — otherwise a long session that
    // spawns and despawns characters would exhaust the pool.
    VkDescriptorSet  skinSet     = VK_NULL_HANDLE;
    VkDescriptorPool skinSetPool = VK_NULL_HANDLE;

    bool     isSkinned()  const { return influenceCount > 0 && skinnedVertexBuffer.get() != VK_NULL_HANDLE; }
    uint32_t vertexCount() const { return vertexCount_; }
    VkBuffer sourceVertexBuffer() const { return vertexBuffer.get(); }

    RenderMesh(const RenderMesh&) = delete;
    RenderMesh& operator=(const RenderMesh&) = delete;

private:
    Buffer   vertexBuffer;
    Buffer   indexBuffer;
    uint32_t indexCount  = 0;
    uint32_t vertexCount_ = 0;   // needed to size the skinning dispatch + output buffer
};
