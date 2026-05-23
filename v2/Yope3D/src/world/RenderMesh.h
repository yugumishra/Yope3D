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

    void destroy(VkDevice device);

    // Bind vertex + index buffers and issue vkCmdDrawIndexed.
    void draw(VkCommandBuffer cmd) const;

    // Rendering properties (set during initialization or per-frame updates)
    Texture*    texture = nullptr;  // Non-owning pointer; nullptr = use default white texture
    float       color[3] = {1.0f, 1.0f, 1.0f};  // Solid color or texture modulation
    int         state = 0;  // Render state: STATE_SOLID (0) or STATE_TEXTURED (1)
    math::Mat4  modelMatrix;  // Updated each frame by physics hull sync
    bool        transformReady = false;  // False until first snapshot propagates; suppresses the 0,0,0 flicker
    float       reflectivity = 0.0f;    // For raytracer: [0,1] mirror reflectance; 0 = fully diffuse

    // Raytracer metadata — set by World after mesh creation when the source is a known Primitive.
    PrimitiveType primitiveType    = PrimitiveType::Custom;
    math::Vec3    primitiveExtents = {1.0f, 1.0f, 1.0f};

    // CPU-side copy of vertex/index data. Retained for Custom triangle-soup packing.
    // Freed (clear + shrink_to_fit) for all other types once primitiveType is determined.
    std::vector<Vertex>   cpuVertices;
    std::vector<uint32_t> cpuIndices;

    RenderMesh(const RenderMesh&) = delete;
    RenderMesh& operator=(const RenderMesh&) = delete;

private:
    Buffer   vertexBuffer;
    Buffer   indexBuffer;
    uint32_t indexCount = 0;
};
