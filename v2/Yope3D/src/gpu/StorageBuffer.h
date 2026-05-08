#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>

class GpuDevice;

// ---------------------------------------------------------------------------
// StorageBuffer
//
// Host-visible, host-coherent VkBuffer kept persistently mapped.
// For SSBO (storage buffer) use: VK_BUFFER_USAGE_STORAGE_BUFFER_BIT.
// Use write() to update data — no staging, no flush required.
// Intended for shader storage buffers (e.g., lights, indirect dispatch data).
// Call destroy() before the GpuDevice is torn down.
// ---------------------------------------------------------------------------

class StorageBuffer {
public:
    StorageBuffer() = default;
    StorageBuffer(GpuDevice& gpu, VkDeviceSize size);

    StorageBuffer(StorageBuffer&&) noexcept;
    StorageBuffer& operator=(StorageBuffer&&) noexcept;
    StorageBuffer(const StorageBuffer&) = delete;
    StorageBuffer& operator=(const StorageBuffer&) = delete;
    ~StorageBuffer() = default;

    void destroy(VkDevice device);
    void write(const void* data, VkDeviceSize size);

    VkBuffer     get()     const { return buffer;  }
    VkDeviceSize getSize() const { return bufSize; }

private:
    static uint32_t findMemoryType(VkPhysicalDevice pd, uint32_t filter,
                                   VkMemoryPropertyFlags props);

    VkBuffer       buffer  = VK_NULL_HANDLE;
    VkDeviceMemory memory  = VK_NULL_HANDLE;
    VkDeviceSize   bufSize = 0;
    void*          mapped  = nullptr;
};
