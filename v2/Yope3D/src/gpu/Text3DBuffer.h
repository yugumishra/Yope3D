#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>

class GpuDevice;

// ---------------------------------------------------------------------------
// Text3DVertex — per-vertex layout for world-space MSDF text quads.
// Positions are glyph-local meters (origin at the label anchor); the vertex
// shader billboards / transforms them. UV indexes the MSDF atlas.
// ---------------------------------------------------------------------------

struct Text3DVertex {
    float x, y, z;      // local position (meters)
    float u, v;         // atlas UV
    float r, g, b, a;   // color
};
static_assert(sizeof(Text3DVertex) == 36, "Text3DVertex must be 36 bytes");

// ---------------------------------------------------------------------------
// Text3DBuffer — persistent-mapped dynamic vertex/index buffer for 3D text,
// rebuilt every frame (mirrors UIBuffer). One instance per frame-in-flight.
// ---------------------------------------------------------------------------

class Text3DBuffer {
public:
    static constexpr uint32_t kMaxVertices = 16384;
    static constexpr uint32_t kMaxIndices  = 65536;

    Text3DBuffer() = default;
    Text3DBuffer(Text3DBuffer&&) noexcept;
    Text3DBuffer& operator=(Text3DBuffer&&) noexcept;
    Text3DBuffer(const Text3DBuffer&) = delete;
    Text3DBuffer& operator=(const Text3DBuffer&) = delete;
    ~Text3DBuffer() = default;

    void init(GpuDevice& gpu);
    void destroy(VkDevice device);

    void begin();

    struct Range {
        uint32_t indexOffset;
        uint32_t indexCount;
        int32_t  vertexOffset;
    };
    Range push(const Text3DVertex* verts, uint32_t vertCount,
               const uint32_t* indices, uint32_t idxCount);

    VkBuffer vertexBuffer() const { return vertBuf_; }
    VkBuffer indexBuffer()  const { return idxBuf_;  }

private:
    VkBuffer       vertBuf_ = VK_NULL_HANDLE;
    VkDeviceMemory vertMem_ = VK_NULL_HANDLE;
    VkBuffer       idxBuf_  = VK_NULL_HANDLE;
    VkDeviceMemory idxMem_  = VK_NULL_HANDLE;
    Text3DVertex*  mappedVerts_ = nullptr;
    uint32_t*      mappedIdx_   = nullptr;
    uint32_t       vertHead_ = 0;
    uint32_t       idxHead_  = 0;

    static uint32_t findMemoryType(VkPhysicalDevice pd, uint32_t filter,
                                   VkMemoryPropertyFlags props);
    static VkBuffer makeBuffer(VkDevice device, VkPhysicalDevice pd,
                               VkDeviceSize size, VkBufferUsageFlags usage,
                               VkDeviceMemory& outMem);
};
