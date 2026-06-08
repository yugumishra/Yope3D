#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include "../rendering/DebugLine.h"

class GpuDevice;

// ---------------------------------------------------------------------------
// LineBuffer — persistent-mapped dynamic vertex buffer for debug lines,
// re-uploaded every frame (mirrors Text3DBuffer, but vertex-only / no index
// buffer since lines are drawn as a flat LINE_LIST). One instance per
// frame-in-flight.
// ---------------------------------------------------------------------------

class LineBuffer {
public:
    static constexpr uint32_t kMaxVertices = 131072;  // 65k segments

    LineBuffer() = default;
    LineBuffer(LineBuffer&&) noexcept;
    LineBuffer& operator=(LineBuffer&&) noexcept;
    LineBuffer(const LineBuffer&) = delete;
    LineBuffer& operator=(const LineBuffer&) = delete;
    ~LineBuffer() = default;

    void init(GpuDevice& gpu);
    void destroy(VkDevice device);

    // Overwrite the buffer from the start. Returns the number of vertices
    // actually uploaded (clamped to kMaxVertices).
    uint32_t upload(const DebugLineVertex* verts, uint32_t count);

    VkBuffer vertexBuffer() const { return vertBuf_; }

private:
    VkBuffer         vertBuf_ = VK_NULL_HANDLE;
    VkDeviceMemory   vertMem_ = VK_NULL_HANDLE;
    DebugLineVertex* mapped_  = nullptr;

    static uint32_t findMemoryType(VkPhysicalDevice pd, uint32_t filter,
                                   VkMemoryPropertyFlags props);
    static VkBuffer makeBuffer(VkDevice device, VkPhysicalDevice pd,
                               VkDeviceSize size, VkBufferUsageFlags usage,
                               VkDeviceMemory& outMem);
};
