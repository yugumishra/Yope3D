#include "PointShadowMap.h"
#include "../gpu/GpuDevice.h"
#include <stdexcept>

PointShadowMap::PointShadowMap(GpuDevice& gpu, VkRenderPass shadowPass) : device_(gpu.device()) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.format        = format_;
    imageInfo.extent        = {RESOLUTION, RESOLUTION, 1};
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = FACES;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device_, &imageInfo, nullptr, &image_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create point shadow map image");

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device_, image_, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = gpu.findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &memory_) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate point shadow map memory");

    vkBindImageMemory(device_, image_, memory_, 0);

    // Sampling view: all 6 layers, sampled in triangle.frag as sampler2DArray
    // (face picked manually by major axis rather than via hardware cube sampling).
    VkImageViewCreateInfo arrayViewInfo{};
    arrayViewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    arrayViewInfo.image    = image_;
    arrayViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    arrayViewInfo.format   = format_;
    arrayViewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    arrayViewInfo.subresourceRange.baseMipLevel   = 0;
    arrayViewInfo.subresourceRange.levelCount     = 1;
    arrayViewInfo.subresourceRange.baseArrayLayer = 0;
    arrayViewInfo.subresourceRange.layerCount     = FACES;

    if (vkCreateImageView(device_, &arrayViewInfo, nullptr, &arrayView_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create point shadow map array view");

    // Per-face single-layer views + framebuffers, one per cube direction, against
    // the same depth-only render pass the directional/spot ShadowMap uses.
    for (uint32_t i = 0; i < FACES; ++i) {
        VkImageViewCreateInfo faceViewInfo{};
        faceViewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        faceViewInfo.image    = image_;
        faceViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        faceViewInfo.format   = format_;
        faceViewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
        faceViewInfo.subresourceRange.baseMipLevel   = 0;
        faceViewInfo.subresourceRange.levelCount     = 1;
        faceViewInfo.subresourceRange.baseArrayLayer = i;
        faceViewInfo.subresourceRange.layerCount     = 1;

        if (vkCreateImageView(device_, &faceViewInfo, nullptr, &faceViews_[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create point shadow map face view");

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass      = shadowPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments    = &faceViews_[i];
        fbInfo.width           = RESOLUTION;
        fbInfo.height          = RESOLUTION;
        fbInfo.layers          = 1;

        if (vkCreateFramebuffer(device_, &fbInfo, nullptr, &framebuffers_[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create point shadow map framebuffer");
    }

    // Clamp-to-edge (not border): unlike the single directional/spot map, every
    // sample here lands inside a face chosen to already contain the fragment, so
    // there's no "outside the frustum -> far plane" case to special-case with a
    // border color — edge clamp just avoids seams at face boundaries.
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter        = VK_FILTER_LINEAR;
    samplerInfo.minFilter        = VK_FILTER_LINEAR;
    samplerInfo.addressModeU     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.compareEnable    = VK_FALSE;
    samplerInfo.mipmapMode       = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.minLod           = 0.0f;
    samplerInfo.maxLod           = 0.0f;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    if (vkCreateSampler(device_, &samplerInfo, nullptr, &sampler_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create point shadow map sampler");
}

PointShadowMap::~PointShadowMap() {
    for (auto fb : framebuffers_) if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(device_, fb, nullptr);
    for (auto v : faceViews_)     if (v != VK_NULL_HANDLE)  vkDestroyImageView(device_, v, nullptr);
    if (sampler_ != VK_NULL_HANDLE)   vkDestroySampler(device_, sampler_, nullptr);
    if (arrayView_ != VK_NULL_HANDLE) vkDestroyImageView(device_, arrayView_, nullptr);
    if (image_ != VK_NULL_HANDLE)     vkDestroyImage(device_, image_, nullptr);
    if (memory_ != VK_NULL_HANDLE)    vkFreeMemory(device_, memory_, nullptr);
}
