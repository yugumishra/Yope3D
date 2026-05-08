#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include "../gpu/Buffer.h"

class GpuDevice;

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

class RenderMesh {
public:
    RenderMesh(GpuDevice& gpu, VkCommandPool commandPool,
               const std::vector<Vertex>&   vertices,
               const std::vector<uint32_t>& indices);

    void destroy(VkDevice device);

    // Bind vertex + index buffers and issue vkCmdDrawIndexed.
    void draw(VkCommandBuffer cmd) const;

    RenderMesh(const RenderMesh&) = delete;
    RenderMesh& operator=(const RenderMesh&) = delete;

private:
    Buffer   vertexBuffer;
    Buffer   indexBuffer;
    uint32_t indexCount = 0;
};
