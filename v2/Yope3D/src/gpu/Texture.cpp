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
// Layout transition helper
// ---------------------------------------------------------------------------

static void transitionImageLayout(VkDevice device, VkCommandPool commandPool,
                                  VkQueue graphicsQueue,
                                  VkImage image, uint32_t mipLevel,
                                  VkImageLayout oldLayout, VkImageLayout newLayout)
{
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

    VkPipelineStageFlags srcStage, dstStage;
    VkImageMemoryBarrier barrier{};
    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout                       = oldLayout;
    barrier.newLayout                       = newLayout;
    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                           = image;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = mipLevel;
    barrier.subresourceRange.levelCount     = 1;
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
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    vkQueueSubmit(graphicsQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
}

// ---------------------------------------------------------------------------
// Texture::load — main factory
// ---------------------------------------------------------------------------

Texture Texture::load(GpuDevice& gpu,
                     VkCommandPool commandPool,
                     VkDescriptorSetLayout set1Layout,
                     VkDescriptorPool texturePool,
                     const uint8_t* pixels, int width, int height)
{
    if (!pixels || width <= 0 || height <= 0)
        throw std::runtime_error("Invalid texture dimensions or null pixel data");

    uint32_t mipLevels = calculateMipLevels(width, height);
    VkDevice device = gpu.device();
    VkQueue graphicsQueue = gpu.graphicsQueue();

    // ---------------------------------------------------------------------------
    // 1. Create VkImage with RGBA8_SRGB format, all mip levels
    // ---------------------------------------------------------------------------

    VkImageCreateInfo imageCI{};
    imageCI.sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCI.imageType   = VK_IMAGE_TYPE_2D;
    imageCI.format      = VK_FORMAT_R8G8B8A8_SRGB;
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
    // 3. Transition base mip from UNDEFINED → TRANSFER_DST_OPTIMAL
    // ---------------------------------------------------------------------------

    transitionImageLayout(device, commandPool, graphicsQueue, image, 0,
                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // ---------------------------------------------------------------------------
    // 4. Upload pixels via staging buffer (only to mip level 0)
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
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    vkQueueSubmit(graphicsQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
    staging.destroy(device);

    // ---------------------------------------------------------------------------
    // 5. Generate mipmaps via vkCmdBlitImage
    // ---------------------------------------------------------------------------

    int mipWidth = width;
    int mipHeight = height;

    for (uint32_t i = 1; i < mipLevels; ++i) {
        // Transition previous mip from TRANSFER_DST → TRANSFER_SRC
        transitionImageLayout(device, commandPool, graphicsQueue, image, i - 1,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        // Transition current mip from UNDEFINED → TRANSFER_DST
        transitionImageLayout(device, commandPool, graphicsQueue, image, i,
                             VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // Allocate command buffer for the blit
        vkAllocateCommandBuffers(device, &ai, &cmd);
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);

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

        vkEndCommandBuffer(cmd);
        vkQueueSubmit(graphicsQueue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue);
        vkFreeCommandBuffers(device, commandPool, 1, &cmd);

        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    // Transition all mips to final SHADER_READ_ONLY_OPTIMAL state.
    // After the loop:
    //   - Mip 0 is in TRANSFER_SRC_OPTIMAL (if mipLevels > 1) or TRANSFER_DST_OPTIMAL (if mipLevels == 1)
    //   - Other mips (1 to last-1) are in TRANSFER_SRC_OPTIMAL (if they exist and were used)
    //   - Last mip is in TRANSFER_DST_OPTIMAL

    // Transition mip 0: handle both cases (1 mip or multiple mips)
    if (mipLevels == 1) {
        // Only one mip: it's in TRANSFER_DST_OPTIMAL
        transitionImageLayout(device, commandPool, graphicsQueue, image, 0,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    } else {
        // Multiple mips: mip 0 was transitioned to TRANSFER_SRC_OPTIMAL in the loop
        transitionImageLayout(device, commandPool, graphicsQueue, image, 0,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    // Transition mips 1 to (last-1) from TRANSFER_SRC → SHADER_READ_ONLY
    for (uint32_t i = 1; i < mipLevels - 1; ++i) {
        transitionImageLayout(device, commandPool, graphicsQueue, image, i,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    // Transition last mip from TRANSFER_DST → SHADER_READ_ONLY (only if there are multiple mips)
    if (mipLevels > 1) {
        transitionImageLayout(device, commandPool, graphicsQueue, image, mipLevels - 1,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    // ---------------------------------------------------------------------------
    // 6. Create VkImageView
    // ---------------------------------------------------------------------------

    VkImageViewCreateInfo viewCI{};
    viewCI.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewCI.image                           = image;
    viewCI.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewCI.format                          = VK_FORMAT_R8G8B8A8_SRGB;
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
    // 7. Create VkSampler (REPEAT, LINEAR_MIPMAP_LINEAR min, LINEAR mag)
    // ---------------------------------------------------------------------------

    VkSamplerCreateInfo samplerCI{};
    samplerCI.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerCI.magFilter               = VK_FILTER_LINEAR;
    samplerCI.minFilter               = VK_FILTER_LINEAR;
    samplerCI.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerCI.addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCI.addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCI.addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCI.anisotropyEnable        = VK_FALSE;
    samplerCI.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerCI.unnormalizedCoordinates = VK_FALSE;
    samplerCI.compareEnable           = VK_FALSE;
    samplerCI.mipLodBias              = 0.0f;
    samplerCI.minLod                  = 0.0f;
    samplerCI.maxLod                  = static_cast<float>(mipLevels);

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
