#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

class GpuDevice;
struct BufferUploadBatch;

// ---------------------------------------------------------------------------
// Buffer
//
// RAII wrapper around a VkBuffer + VkDeviceMemory pair.
// Use uploadStaged() to create a device-local buffer populated from host data.
// Call destroy() before the GpuDevice is destroyed — the destructor is a no-op
// because destroying requires the VkDevice handle.
// ---------------------------------------------------------------------------

class Buffer {
public:
    // Upload host data to a device-local buffer via a host-visible staging copy.
    // commandPool must support transfer operations (the graphics queue is fine).
    static Buffer uploadStaged(GpuDevice& gpu, VkCommandPool commandPool,
                               const void* data, VkDeviceSize size,
                               VkBufferUsageFlags dstUsage);

    // Deferred variant: creates the device-local buffer + a staging copy and
    // RECORDS the copy into batch.cmd (no submit / no wait). The staging buffer is
    // moved into batch.staging and must outlive the submit; the caller submits the
    // command buffer with a fence, waits, then destroys the staging buffers.
    static Buffer uploadStagedDeferred(GpuDevice& gpu, BufferUploadBatch& batch,
                                       const void* data, VkDeviceSize size,
                                       VkBufferUsageFlags dstUsage);

    // Create a buffer with specified usage and memory properties.
    static Buffer create(GpuDevice& gpu, VkDeviceSize size,
                        VkBufferUsageFlags usage, VkMemoryPropertyFlags props);

    Buffer() = default;
    Buffer(Buffer&&) noexcept;
    Buffer& operator=(Buffer&&) noexcept;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    ~Buffer() = default;

    void destroy(VkDevice device);

    VkBuffer       get()        const { return buffer; }
    VkDeviceMemory getMemory()  const { return memory; }
    VkDeviceSize   getSize()    const { return size;   }

    //map memory method to allow for persistent memory mapped buffers
    void mapMemory(GpuDevice& gpu, void* mappedPointer);

private:
    Buffer(VkBuffer buf, VkDeviceMemory mem, VkDeviceSize sz)
        : buffer(buf), memory(mem), size(sz) {}

    static uint32_t findMemoryType(VkPhysicalDevice pd, uint32_t filter,
                                   VkMemoryPropertyFlags props);

    VkBuffer       buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize   size   = 0;
};

// A set of deferred buffer copies recorded into one command buffer for a single
// batched submit (instead of the per-buffer vkQueueWaitIdle stall in
// uploadStaged). The caller owns the command buffer, submits it with a fence,
// waits, then destroys the retained staging buffers. Used by the async scene-load
// commit pump to upload many meshes with one fenced submit per frame.
struct BufferUploadBatch {
    VkCommandBuffer     cmd = VK_NULL_HANDLE;
    std::vector<Buffer> staging;   // retained until the submit fence signals
};
