#include "DepthBuffer.h"
#include "GpuDevice.h"
#include <stdexcept>

DepthBuffer::DepthBuffer(GpuDevice& gpu, uint32_t width, uint32_t height)
    : device(gpu.device()), depthFormat(VK_FORMAT_D32_SFLOAT) {

    // Create depth image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType   = VK_IMAGE_TYPE_2D;
    imageInfo.format      = depthFormat;
    imageInfo.extent      = {width, height, 1};
    imageInfo.mipLevels   = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples     = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling      = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage       = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device, &imageInfo, nullptr, &img) != VK_SUCCESS)
        throw std::runtime_error("Failed to create depth image");

    // Allocate memory
    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device, img, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = gpu.findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &mem) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate depth image memory");

    vkBindImageMemory(device, img, mem, 0);

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image    = img;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format   = depthFormat;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS)
        throw std::runtime_error("Failed to create depth image view");
}

DepthBuffer::~DepthBuffer() {
    if (view != VK_NULL_HANDLE)
        vkDestroyImageView(device, view, nullptr);
    if (img != VK_NULL_HANDLE)
        vkDestroyImage(device, img, nullptr);
    if (mem != VK_NULL_HANDLE)
        vkFreeMemory(device, mem, nullptr);
}
