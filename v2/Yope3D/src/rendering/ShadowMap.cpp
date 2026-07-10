#include "ShadowMap.h"
#include "../gpu/GpuDevice.h"
#include <stdexcept>

ShadowMap::ShadowMap(GpuDevice& gpu, VkRenderPass shadowPass) : device_(gpu.device()) {
    // Depth image: attachment (write, in the shadow pass) + sampled (read, in the
    // main pass). initialLayout UNDEFINED — the shadow pass's own layout transition
    // (via its attachment description) takes it to DEPTH_STENCIL_READ_ONLY_OPTIMAL.
    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.format        = format_;
    imageInfo.extent        = {RESOLUTION, RESOLUTION, 1};
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device_, &imageInfo, nullptr, &image_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shadow map image");

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device_, image_, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = gpu.findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &memory_) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate shadow map memory");

    vkBindImageMemory(device_, image_, memory_, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image    = image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format   = format_;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(device_, &viewInfo, nullptr, &view_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shadow map image view");

    // Plain (non-comparison) sampler: the shader does the depth compare manually
    // (see triangle.frag) instead of via a hardware comparison sampler — MoltenVK's
    // portability subset only supports compareEnable=VK_TRUE when the optional
    // mutableComparisonSamplers feature is present, which isn't guaranteed. Border
    // color is opaque white (depth=1.0 = far plane), so out-of-frustum samples read
    // as "far / no occluder" and the manual compare naturally resolves to lit.
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter        = VK_FILTER_LINEAR;
    samplerInfo.minFilter        = VK_FILTER_LINEAR;
    samplerInfo.addressModeU     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor      = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.compareEnable    = VK_FALSE;
    samplerInfo.mipmapMode       = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.minLod           = 0.0f;
    samplerInfo.maxLod           = 0.0f;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    if (vkCreateSampler(device_, &samplerInfo, nullptr, &sampler_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shadow map sampler");

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass      = shadowPass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments    = &view_;
    fbInfo.width           = RESOLUTION;
    fbInfo.height          = RESOLUTION;
    fbInfo.layers          = 1;

    if (vkCreateFramebuffer(device_, &fbInfo, nullptr, &framebuffer_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shadow map framebuffer");
}

ShadowMap::~ShadowMap() {
    if (framebuffer_ != VK_NULL_HANDLE) vkDestroyFramebuffer(device_, framebuffer_, nullptr);
    if (sampler_ != VK_NULL_HANDLE)     vkDestroySampler(device_, sampler_, nullptr);
    if (view_ != VK_NULL_HANDLE)        vkDestroyImageView(device_, view_, nullptr);
    if (image_ != VK_NULL_HANDLE)       vkDestroyImage(device_, image_, nullptr);
    if (memory_ != VK_NULL_HANDLE)      vkFreeMemory(device_, memory_, nullptr);
}
