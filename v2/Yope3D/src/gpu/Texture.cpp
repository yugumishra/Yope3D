#include "Texture.h"
#include "GpuDevice.h"
#include "Buffer.h"
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <algorithm>

// ---------------------------------------------------------------------------
// Mip level calculation
// ---------------------------------------------------------------------------

uint32_t Texture::calculateMipLevels(int width, int height) {
    int maxDim = std::max(width, height);
    return static_cast<uint32_t>(std::floor(std::log2(maxDim))) + 1;
}

// ---------------------------------------------------------------------------
// Memory type lookup (same as Buffer)
// ---------------------------------------------------------------------------

uint32_t Texture::findMemoryType(VkPhysicalDevice pd, uint32_t filter,
                                 VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(pd, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        if ((filter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("Failed to find suitable memory type for texture");
}

// ---------------------------------------------------------------------------
// Layout transition barrier — records into an already-open command buffer
// (no allocate/submit/wait of its own). Texture::load records every transition,
// the buffer-to-image copy, and every mip blit into ONE command buffer with a
// single fenced submit at the end, instead of a separate vkQueueSubmit +
// vkQueueWaitIdle per operation (~30 GPU-drain stalls per texture otherwise —
// the dominant cost of the old synchronous load path).
// ---------------------------------------------------------------------------

static void recordLayoutTransition(VkCommandBuffer cmd, VkImage image,
                                   uint32_t baseMip, uint32_t levelCount,
                                   VkImageLayout oldLayout, VkImageLayout newLayout)
{
    VkPipelineStageFlags srcStage, dstStage;
    VkImageMemoryBarrier barrier{};
    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout                       = oldLayout;
    barrier.newLayout                       = newLayout;
    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                           = image;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = baseMip;
    barrier.subresourceRange.levelCount     = levelCount;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        throw std::runtime_error("Unsupported image layout transition");
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

// ---------------------------------------------------------------------------
// Texture::load — main factory
// ---------------------------------------------------------------------------

Texture Texture::load(GpuDevice& gpu,
                     VkCommandPool commandPool,
                     VkDescriptorSetLayout set1Layout,
                     VkDescriptorPool texturePool,
                     const uint8_t* pixels, int width, int height,
                     bool generateMipmaps, bool srgb)
{
    if (!pixels || width <= 0 || height <= 0)
        throw std::runtime_error("Invalid texture dimensions or null pixel data");

    const VkFormat format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    uint32_t mipLevels = generateMipmaps ? calculateMipLevels(width, height) : 1u;
    VkDevice device = gpu.device();
    VkQueue graphicsQueue = gpu.graphicsQueue();

    if (generateMipmaps && mipLevels > 1) {
        VkFormatProperties fmtProps{};
        vkGetPhysicalDeviceFormatProperties(gpu.physicalDevice(), format, &fmtProps);
        if (!(fmtProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
            throw std::runtime_error("Texture format does not support linear blitting for mipmap generation");
    }

    // ---------------------------------------------------------------------------
    // 1. Create VkImage with RGBA8_SRGB format, all mip levels
    // ---------------------------------------------------------------------------

    VkImageCreateInfo imageCI{};
    imageCI.sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCI.imageType   = VK_IMAGE_TYPE_2D;
    imageCI.format      = format;
    imageCI.extent      = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    imageCI.mipLevels   = mipLevels;
    imageCI.arrayLayers = 1;
    imageCI.samples     = VK_SAMPLE_COUNT_1_BIT;
    imageCI.tiling      = VK_IMAGE_TILING_OPTIMAL;
    imageCI.usage       = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                          VK_IMAGE_USAGE_SAMPLED_BIT |
                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT;  // TRANSFER_SRC for blitting mipmaps
    imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkImage image;
    if (vkCreateImage(device, &imageCI, nullptr, &image) != VK_SUCCESS)
        throw std::runtime_error("Failed to create texture image");

    // ---------------------------------------------------------------------------
    // 2. Allocate and bind device memory
    // ---------------------------------------------------------------------------

    VkMemoryRequirements memReq{};
    vkGetImageMemoryRequirements(device, image, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(gpu.physicalDevice(), memReq.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkDeviceMemory memory;
    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyImage(device, image, nullptr);
        throw std::runtime_error("Failed to allocate texture memory");
    }

    vkBindImageMemory(device, image, memory, 0);

    // ---------------------------------------------------------------------------
    // 3-5. Upload pixels + generate mipmaps — all recorded into ONE command
    // buffer with a single fenced submit (see recordLayoutTransition comment).
    // ---------------------------------------------------------------------------

    VkDeviceSize pixelDataSize = static_cast<VkDeviceSize>(width * height * 4);

    Buffer staging = Buffer::create(gpu, pixelDataSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void* mappedData;
    vkMapMemory(device, staging.getMemory(), 0, pixelDataSize, 0, &mappedData);
    std::memcpy(mappedData, pixels, static_cast<size_t>(pixelDataSize));
    vkUnmapMemory(device, staging.getMemory());

    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = commandPool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &ai, &cmd);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    // Transition every mip level to TRANSFER_DST up front (valid even though only
    // mip 0 is written by the copy below — mips >0 get written by the blit loop,
    // and are already in the right layout for vkCmdBlitImage's dst by the time
    // each is targeted).
    recordLayoutTransition(cmd, image, 0, mipLevels,
                           VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy region{};
    region.bufferOffset                    = 0;
    region.bufferRowLength                 = 0;
    region.bufferImageHeight               = 0;
    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount     = 1;
    region.imageOffset                     = {0, 0, 0};
    region.imageExtent                     = {static_cast<uint32_t>(width),
                                               static_cast<uint32_t>(height), 1};

    vkCmdCopyBufferToImage(cmd, staging.get(), image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          1, &region);

    // Generate mipmaps via vkCmdBlitImage (skipped when generateMipmaps=false).
    int mipWidth = width;
    int mipHeight = height;

    for (uint32_t i = 1; generateMipmaps && i < mipLevels; ++i) {
        // Previous mip TRANSFER_DST → TRANSFER_SRC so it can serve as this blit's
        // source. Mip i is already TRANSFER_DST from the bulk transition above.
        recordLayoutTransition(cmd, image, i - 1, 1,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        VkImageBlit blit{};
        blit.srcOffsets[0]                   = {0, 0, 0};
        blit.srcOffsets[1]                   = {mipWidth, mipHeight, 1};
        blit.srcSubresource.aspectMask       = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel         = i - 1;
        blit.srcSubresource.baseArrayLayer   = 0;
        blit.srcSubresource.layerCount       = 1;
        blit.dstOffsets[0]                   = {0, 0, 0};
        blit.dstOffsets[1]                   = {mipWidth > 1 ? mipWidth / 2 : 1,
                                                  mipHeight > 1 ? mipHeight / 2 : 1, 1};
        blit.dstSubresource.aspectMask       = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel         = i;
        blit.dstSubresource.baseArrayLayer   = 0;
        blit.dstSubresource.layerCount       = 1;

        vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      1, &blit, VK_FILTER_LINEAR);

        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    // Transition to final SHADER_READ_ONLY_OPTIMAL. After the loop above:
    //   - mips [0, mipLevels-2] are TRANSFER_SRC_OPTIMAL (each was a blit source)
    //   - the last mip is still TRANSFER_DST_OPTIMAL (never blitted from)
    //   - with mipLevels==1, the only mip is TRANSFER_DST_OPTIMAL (no blits ran)
    if (mipLevels == 1) {
        recordLayoutTransition(cmd, image, 0, 1,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    } else {
        recordLayoutTransition(cmd, image, 0, mipLevels - 1,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        recordLayoutTransition(cmd, image, mipLevels - 1, 1,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    vkEndCommandBuffer(cmd);

    VkFenceCreateInfo fenceCI{};
    fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence;
    if (vkCreateFence(device, &fenceCI, nullptr, &fence) != VK_SUCCESS)
        throw std::runtime_error("Failed to create texture upload fence");

    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    vkQueueSubmit(graphicsQueue, 1, &si, fence);
    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

    vkDestroyFence(device, fence, nullptr);
    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
    staging.destroy(device);

    // ---------------------------------------------------------------------------
    // 6. Create VkImageView
    // ---------------------------------------------------------------------------

    VkImageViewCreateInfo viewCI{};
    viewCI.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewCI.image                           = image;
    viewCI.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewCI.format                          = format;
    viewCI.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    viewCI.subresourceRange.baseMipLevel   = 0;
    viewCI.subresourceRange.levelCount     = mipLevels;
    viewCI.subresourceRange.baseArrayLayer = 0;
    viewCI.subresourceRange.layerCount     = 1;

    VkImageView imageView;
    if (vkCreateImageView(device, &viewCI, nullptr, &imageView) != VK_SUCCESS) {
        vkDestroyImage(device, image, nullptr);
        vkFreeMemory(device, memory, nullptr);
        throw std::runtime_error("Failed to create texture image view");
    }

    // ---------------------------------------------------------------------------
    // 7. Create VkSampler
    //    generateMipmaps=true:  REPEAT, LINEAR_MIPMAP_LINEAR (3D textures)
    //    generateMipmaps=false: CLAMP_TO_EDGE, NEAREST mipmap, maxLod=0 (font atlases)
    // ---------------------------------------------------------------------------

    VkSamplerCreateInfo samplerCI{};
    samplerCI.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerCI.magFilter               = VK_FILTER_LINEAR;
    samplerCI.minFilter               = VK_FILTER_LINEAR;
    samplerCI.mipmapMode              = generateMipmaps ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                                        : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerCI.addressModeU            = generateMipmaps ? VK_SAMPLER_ADDRESS_MODE_REPEAT
                                                        : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.addressModeV            = generateMipmaps ? VK_SAMPLER_ADDRESS_MODE_REPEAT
                                                        : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.addressModeW            = generateMipmaps ? VK_SAMPLER_ADDRESS_MODE_REPEAT
                                                        : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.anisotropyEnable        = VK_FALSE;
    samplerCI.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerCI.unnormalizedCoordinates = VK_FALSE;
    samplerCI.compareEnable           = VK_FALSE;
    samplerCI.mipLodBias              = 0.0f;
    samplerCI.minLod                  = 0.0f;
    samplerCI.maxLod                  = generateMipmaps ? static_cast<float>(mipLevels) : 0.0f;

    VkSampler sampler;
    if (vkCreateSampler(device, &samplerCI, nullptr, &sampler) != VK_SUCCESS) {
        vkDestroyImageView(device, imageView, nullptr);
        vkDestroyImage(device, image, nullptr);
        vkFreeMemory(device, memory, nullptr);
        throw std::runtime_error("Failed to create sampler");
    }

    // ---------------------------------------------------------------------------
    // 8. Allocate VkDescriptorSet for set 1
    // ---------------------------------------------------------------------------

    VkDescriptorSetAllocateInfo dsAllocInfo{};
    dsAllocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAllocInfo.descriptorPool     = texturePool;
    dsAllocInfo.descriptorSetCount = 1;
    dsAllocInfo.pSetLayouts        = &set1Layout;

    VkDescriptorSet descriptorSet;
    if (vkAllocateDescriptorSets(device, &dsAllocInfo, &descriptorSet) != VK_SUCCESS) {
        vkDestroySampler(device, sampler, nullptr);
        vkDestroyImageView(device, imageView, nullptr);
        vkDestroyImage(device, image, nullptr);
        vkFreeMemory(device, memory, nullptr);
        throw std::runtime_error("Failed to allocate texture descriptor set");
    }

    // ---------------------------------------------------------------------------
    // 9. Write descriptor set with image view + sampler
    // ---------------------------------------------------------------------------

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler      = sampler;
    imageInfo.imageView    = imageView;
    imageInfo.imageLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = descriptorSet;
    write.dstBinding      = 0;
    write.dstArrayElement = 0;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo      = &imageInfo;

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    return Texture(image, memory, imageView, sampler, descriptorSet, mipLevels);
}

// ---------------------------------------------------------------------------
// Move semantics
// ---------------------------------------------------------------------------

Texture::Texture(Texture&& o) noexcept
    : image(o.image), memory(o.memory), imageView(o.imageView), sampler(o.sampler),
      descriptorSet(o.descriptorSet), mipLevels(o.mipLevels)
{
    o.image = VK_NULL_HANDLE;
    o.memory = VK_NULL_HANDLE;
    o.imageView = VK_NULL_HANDLE;
    o.sampler = VK_NULL_HANDLE;
    o.descriptorSet = VK_NULL_HANDLE;
    o.mipLevels = 1;
}

Texture& Texture::operator=(Texture&& o) noexcept {
    image = o.image;
    memory = o.memory;
    imageView = o.imageView;
    sampler = o.sampler;
    descriptorSet = o.descriptorSet;
    mipLevels = o.mipLevels;
    o.image = VK_NULL_HANDLE;
    o.memory = VK_NULL_HANDLE;
    o.imageView = VK_NULL_HANDLE;
    o.sampler = VK_NULL_HANDLE;
    o.descriptorSet = VK_NULL_HANDLE;
    o.mipLevels = 1;
    return *this;
}

void Texture::destroy(VkDevice device) {
    if (sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, sampler, nullptr);
        sampler = VK_NULL_HANDLE;
    }
    if (imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, imageView, nullptr);
        imageView = VK_NULL_HANDLE;
    }
    if (image != VK_NULL_HANDLE) {
        vkDestroyImage(device, image, nullptr);
        image = VK_NULL_HANDLE;
    }
    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory, nullptr);
        memory = VK_NULL_HANDLE;
    }
    // Do NOT destroy descriptorSet — pool cleanup handles it
    descriptorSet = VK_NULL_HANDLE;
}
