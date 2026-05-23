#include "StorageImage.h"
#include "GpuDevice.h"
#include <stdexcept>

StorageImage::StorageImage(GpuDevice& gpu, uint32_t width, uint32_t height, VkFormat format,
                           VkCommandPool cmdPool) {
    create(gpu, width, height, format, cmdPool);
}

StorageImage::~StorageImage() {
    // destroy() must be called explicitly by the owner before this destructor fires;
    // the device handle may already be gone if we let RAII run blind.
}

void StorageImage::destroy(VkDevice device) {
    if (sampler_ != VK_NULL_HANDLE) { vkDestroySampler(device, sampler_, nullptr); sampler_ = VK_NULL_HANDLE; }
    if (view_    != VK_NULL_HANDLE) { vkDestroyImageView(device, view_, nullptr);  view_    = VK_NULL_HANDLE; }
    if (image_   != VK_NULL_HANDLE) { vkDestroyImage(device, image_, nullptr);     image_   = VK_NULL_HANDLE; }
    if (memory_  != VK_NULL_HANDLE) { vkFreeMemory(device, memory_, nullptr);      memory_  = VK_NULL_HANDLE; }
    width_ = height_ = 0;
}

void StorageImage::recreate(GpuDevice& gpu, uint32_t newWidth, uint32_t newHeight, VkCommandPool cmdPool) {
    destroy(device_);
    create(gpu, newWidth, newHeight, format_, cmdPool);
}

StorageImage::StorageImage(StorageImage&& o) noexcept
    : image_(o.image_), memory_(o.memory_), view_(o.view_), sampler_(o.sampler_),
      format_(o.format_), width_(o.width_), height_(o.height_), device_(o.device_) {
    o.image_   = VK_NULL_HANDLE;
    o.memory_  = VK_NULL_HANDLE;
    o.view_    = VK_NULL_HANDLE;
    o.sampler_ = VK_NULL_HANDLE;
    o.device_  = VK_NULL_HANDLE;
}

StorageImage& StorageImage::operator=(StorageImage&& o) noexcept {
    if (this != &o) {
        image_   = o.image_;   memory_  = o.memory_;
        view_    = o.view_;    sampler_ = o.sampler_;
        format_  = o.format_;  width_   = o.width_;  height_ = o.height_;
        device_  = o.device_;
        o.image_   = VK_NULL_HANDLE;
        o.memory_  = VK_NULL_HANDLE;
        o.view_    = VK_NULL_HANDLE;
        o.sampler_ = VK_NULL_HANDLE;
        o.device_  = VK_NULL_HANDLE;
    }
    return *this;
}

void StorageImage::transition(VkCommandBuffer cmd,
                               VkImageLayout oldLayout, VkImageLayout newLayout,
                               VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                               VkAccessFlags srcAccess, VkAccessFlags dstAccess) const {
    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = oldLayout;
    barrier.newLayout           = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = image_;
    barrier.srcAccessMask       = srcAccess;
    barrier.dstAccessMask       = dstAccess;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void StorageImage::create(GpuDevice& gpu, uint32_t w, uint32_t h, VkFormat fmt, VkCommandPool cmdPool) {
    device_ = gpu.device();
    format_ = fmt;
    width_  = w;
    height_ = h;

    // Create image
    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = fmt;
    ici.extent        = { w, h, 1 };
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(gpu.device(), &ici, nullptr, &image_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create storage image");

    // Allocate device-local memory
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(gpu.device(), image_, &mr);
    VkMemoryAllocateInfo mai{};
    mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize  = mr.size;
    mai.memoryTypeIndex = gpu.findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(gpu.device(), &mai, nullptr, &memory_) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate storage image memory");
    vkBindImageMemory(gpu.device(), image_, memory_, 0);

    // Image view
    VkImageViewCreateInfo vci{};
    vci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image                           = image_;
    vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    vci.format                          = fmt;
    vci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.baseMipLevel   = 0;
    vci.subresourceRange.levelCount     = 1;
    vci.subresourceRange.baseArrayLayer = 0;
    vci.subresourceRange.layerCount     = 1;
    if (vkCreateImageView(gpu.device(), &vci, nullptr, &view_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create storage image view");

    // Sampler (linear, clamp to edge)
    VkSamplerCreateInfo sci{};
    sci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter    = VK_FILTER_LINEAR;
    sci.minFilter    = VK_FILTER_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(gpu.device(), &sci, nullptr, &sampler_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create storage image sampler");

    // Transition to GENERAL layout immediately using a one-shot command buffer
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = cmdPool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer tmpCmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(gpu.device(), &ai, &tmpCmd);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(tmpCmd, &bi);

    transition(tmpCmd,
               VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
               0, VK_ACCESS_SHADER_WRITE_BIT);

    vkEndCommandBuffer(tmpCmd);
    VkSubmitInfo sub{};
    sub.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    sub.commandBufferCount = 1;
    sub.pCommandBuffers    = &tmpCmd;
    vkQueueSubmit(gpu.graphicsQueue(), 1, &sub, VK_NULL_HANDLE);
    vkQueueWaitIdle(gpu.graphicsQueue());
    vkFreeCommandBuffers(gpu.device(), cmdPool, 1, &tmpCmd);
}
