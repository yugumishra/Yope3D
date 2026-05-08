#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>

class GpuDevice;

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

    Buffer() = default;
    Buffer(Buffer&&) noexcept;
    Buffer& operator=(Buffer&&) noexcept;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    ~Buffer() = default;

    void destroy(VkDevice device);

    VkBuffer     get()     const { return buffer; }
    VkDeviceSize getSize() const { return size;   }

    //map memory method to allow for persistent memory mapped buffers
    void mapMemory(GpuDevice& gpu, void* mappedPointer);

private:
    Buffer(VkBuffer buf, VkDeviceMemory mem, VkDeviceSize sz)
        : buffer(buf), memory(mem), size(sz) {}

    static Buffer   create(GpuDevice& gpu, VkDeviceSize size,
                           VkBufferUsageFlags usage, VkMemoryPropertyFlags props);
    static uint32_t findMemoryType(VkPhysicalDevice pd, uint32_t filter,
                                   VkMemoryPropertyFlags props);

    VkBuffer       buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize   size   = 0;
};
