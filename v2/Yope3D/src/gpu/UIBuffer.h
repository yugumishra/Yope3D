#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>

class GpuDevice;

// ---------------------------------------------------------------------------
// UIVertex — per-vertex layout for the UI pipeline.
// Positions are in NDC (converted from [0,1] percentage coords by the CPU).
// ---------------------------------------------------------------------------

struct UIVertex {
    float x, y;       // NDC position (-1..1)
    float u, v;       // texture UV (0..1)
    float r, g, b, a; // RGBA color (0..1)
};
static_assert(sizeof(UIVertex) == 32, "UIVertex must be 32 bytes");

// ---------------------------------------------------------------------------
// UIDrawCall — one draw command for the UI render pass.
// Produced by UIManager::buildFrame() and consumed by Renderer.
// ---------------------------------------------------------------------------

struct UIDrawCall {
    uint32_t        indexCount;
    uint32_t        indexOffset;
    int32_t         vertexOffset;  // base vertex added to every index
    int32_t         state;         // 0=solid 1=textured 2=MSDF text
    VkDescriptorSet texture;       // VK_NULL_HANDLE if state==0
    float           distanceRange = 0.0f;  // MSDF texel range (state==2); feeds shader AA
};

// ---------------------------------------------------------------------------
// UIBuffer
//
// Double-written every frame: call begin() to reset, push() to append batches,
// then the Renderer binds the underlying VkBuffers for drawing.
// One instance per frame-in-flight; owned by Renderer.
// ---------------------------------------------------------------------------

class UIBuffer {
public:
    static constexpr uint32_t kMaxVertices = 16384;  // 16384 × 32 B = 512 KB
    static constexpr uint32_t kMaxIndices  = 65536;  // 65536 × 4 B  = 256 KB

    UIBuffer() = default;
    UIBuffer(UIBuffer&&) noexcept;
    UIBuffer& operator=(UIBuffer&&) noexcept;
    UIBuffer(const UIBuffer&) = delete;
    UIBuffer& operator=(const UIBuffer&) = delete;
    ~UIBuffer() = default;

    void init(GpuDevice& gpu);
    void destroy(VkDevice device);

    // Reset write heads for this frame (call once per frame before push()).
    void begin();

    // Append vertices+indices; returns the range for a DrawIndexed call.
    // Indices must be relative (0-based within this batch); vertexOffset is added by the GPU.
    struct Range {
        uint32_t indexOffset;
        uint32_t indexCount;
        int32_t  vertexOffset;  // base vertex for the index buffer
    };
    Range push(const UIVertex* verts, uint32_t vertCount,
               const uint32_t* indices, uint32_t idxCount);

    VkBuffer vertexBuffer() const { return vertBuf_; }
    VkBuffer indexBuffer()  const { return idxBuf_;  }

private:
    VkBuffer       vertBuf_ = VK_NULL_HANDLE;
    VkDeviceMemory vertMem_ = VK_NULL_HANDLE;
    VkBuffer       idxBuf_  = VK_NULL_HANDLE;
    VkDeviceMemory idxMem_  = VK_NULL_HANDLE;
    UIVertex*      mappedVerts_ = nullptr;
    uint32_t*      mappedIdx_   = nullptr;
    uint32_t       vertHead_ = 0;
    uint32_t       idxHead_  = 0;

    static uint32_t findMemoryType(VkPhysicalDevice pd, uint32_t filter,
                                   VkMemoryPropertyFlags props);
    static VkBuffer makeBuffer(VkDevice device, VkPhysicalDevice pd,
                               VkDeviceSize size, VkBufferUsageFlags usage,
                               VkDeviceMemory& outMem);
};
