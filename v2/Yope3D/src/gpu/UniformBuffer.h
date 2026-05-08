#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>

class GpuDevice;

// ---------------------------------------------------------------------------
// UniformBuffer
//
// Host-visible, host-coherent VkBuffer kept persistently mapped.
// Use write() each frame — no staging, no flush required.
// Intended for small, per-frame data like GlobalUBO (view/proj/cameraPos).
// Call destroy() before the GpuDevice is torn down.
// ---------------------------------------------------------------------------

class UniformBuffer {
public:
    UniformBuffer() = default;
    UniformBuffer(GpuDevice& gpu, VkDeviceSize size);

    UniformBuffer(UniformBuffer&&) noexcept;
    UniformBuffer& operator=(UniformBuffer&&) noexcept;
    UniformBuffer(const UniformBuffer&) = delete;
    UniformBuffer& operator=(const UniformBuffer&) = delete;
    ~UniformBuffer() = default;

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
