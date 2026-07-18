#pragma once
#include <vulkan/vulkan.h>

class GpuDevice;

class DepthBuffer {
public:
    DepthBuffer(GpuDevice& gpu, uint32_t width, uint32_t height);
    ~DepthBuffer();

    VkImageView imageView() const { return view; }
    VkImage image() const { return img; }
    VkFormat format() const { return depthFormat; }

    DepthBuffer(const DepthBuffer&) = delete;
    DepthBuffer& operator=(const DepthBuffer&) = delete;

private:
    VkDevice       device = VK_NULL_HANDLE;
    VkImage        img    = VK_NULL_HANDLE;
    VkDeviceMemory mem    = VK_NULL_HANDLE;
    VkImageView    view   = VK_NULL_HANDLE;
    VkFormat       depthFormat;
};
