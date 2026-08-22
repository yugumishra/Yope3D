#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <cstdint>
#include "../gpu/Buffer.h"
#include "../gpu/DynamicMeshBuffer.h"
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

    // ---- Dynamic meshes ----
    //
    // A dynamic mesh has its geometry rewritten from the CPU instead of being
    // uploaded once at construction. In place of one device-local vertex buffer
    // it owns kFramesInFlight host-visible rings (DynamicMeshBuffer), because a
    // slot may only be written after drawFrame's vkWaitForFences has retired
    // that slot's submission. That is why uploadDynamic() is driven by the
    // Renderer (via World::uploadDynamicMeshes) and NOT by whoever calls
    // setDynamicGeometry(): the caller is a script, running well outside the
    // window. uiBuffers / text3DBuffers_ / lineBuffers_ live under the same
    // rule; this is a fourth tenant of an existing mechanism, not a new one.
    //
    // ---- COST: a dynamic update is NOT the price of drawing a static mesh ----
    //
    // Read this before choosing a dynamic mesh over a static one. The expensive
    // part is not the upload, it is the repack. setDynamicGeometry() re-runs the
    // whole meshbuild::buildPacked() pipeline on every single call:
    //
    //   * computeTangents — a pass over every triangle accumulating per-vertex
    //     tangents from UV gradients, into two temporary std::vector<math::Vec3>
    //     allocations the size of the vertex array, followed by a per-vertex
    //     Gram-Schmidt orthonormalise. This is the larger half of the cost.
    //   * packVertices — a per-vertex octahedral snorm16 encode of the normal
    //     and tangent into the 32-byte PackedVertex GPU layout.
    //
    // That is O(triangles + vertices) of CPU work per update, redone from
    // scratch whether one vertex moved or all of them did — there is no delta
    // path. A static mesh pays exactly this once, at load, and then costs
    // nothing per frame. Order-of-magnitude: a few thousand vertices lands in
    // the tens-to-low-hundreds of microseconds per update. That is comfortable
    // at a capped update rate; it is not free at 60 Hz on a dense mesh, and it
    // scales with the whole array rather than with what actually changed.
    //
    // So: update at the lowest rate the visual tolerates, keep vertex counts
    // deliberate, and do not reach for a dynamic mesh to animate something a
    // Transform could have moved.
    //
    // Future work, in the order it would pay off (all interior to this class —
    // none of them change the API below):
    //   1. Skip computeTangents when the material has no normal map. It is the
    //      larger half of the cost and pure waste for a flat-shaded surface.
    //   2. Accept pre-packed PackedVertex, letting a producer that already
    //      knows its tangent frame bypass the derivation entirely.
    //   3. Dirty-range updates, so a partial edit copies a sub-range instead of
    //      re-encoding the whole array.

    // Ring depth. Must equal Renderer::MAX_FRAMES — static_assert'd in
    // Renderer.cpp, since this header must not depend on the renderer.
    static constexpr uint32_t kFramesInFlight = 2;

    // Disambiguates the dynamic constructor from the two upload constructors.
    struct DynamicTag {};

    // Dynamic-mesh constructor. Allocates the rings at the given capacities and
    // leaves the mesh drawing nothing until the first setDynamicGeometry().
    // Capacity is fixed here: writes beyond it are clamped, not grown.
    RenderMesh(GpuDevice& gpu, DynamicTag,
               uint32_t maxVertices, uint32_t maxIndices);

    bool     isDynamic()          const { return dynamic_; }
    uint32_t dynamicMaxVertices() const { return dynMaxVerts_; }
    uint32_t dynamicMaxIndices()  const { return dynMaxIndices_; }

    // Repack `vertices` and stage them for the Renderer to upload. See the COST
    // note above — this is the expensive call. Returns false (changing nothing,
    // so the mesh keeps drawing its previous contents) if the mesh is not
    // dynamic, if the arrays exceed the capacities fixed at construction, or if
    // an index is out of range for the vertex array.
    bool setDynamicGeometry(const std::vector<Vertex>&   vertices,
                            const std::vector<uint32_t>& indices);

    // Copy staged geometry into ring slot `slot` and make it the slot draw()
    // binds. MUST be called inside the frame-fence window; World::uploadDynamicMeshes
    // is the only caller. Cheap and idempotent — a mesh that has stopped updating
    // stops memcpying once every slot holds the current geometry.
    void uploadDynamic(uint32_t slot);

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

    // ---- Dynamic-mesh state (see the COST note in the public section) ----
    bool     dynamic_       = false;
    uint32_t dynMaxVerts_   = 0;
    uint32_t dynMaxIndices_ = 0;
    uint32_t drawSlot_      = 0;   // ring slot draw() binds; set by uploadDynamic
    // One "needs the current staged geometry" bit per slot. setDynamicGeometry
    // sets them all; uploadDynamic clears the one it writes. Without this a mesh
    // that stopped updating would keep memcpying identical bytes every frame
    // forever; with it, copying stops kFramesInFlight frames after the last
    // update and resumes only on the next one.
    uint32_t dynDirtyMask_  = 0;
    std::array<DynamicMeshBuffer, kFramesInFlight> dynRing_;
    // Staged packed geometry, held between setDynamicGeometry (any thread the
    // script runs on, outside the window) and uploadDynamic (render thread,
    // inside it). Reused across updates so a steady-state stream does not
    // reallocate.
    std::vector<PackedVertex> dynStagedVerts_;
    std::vector<uint32_t>     dynStagedIndices_;
};
