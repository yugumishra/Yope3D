#include "Skybox.h"
#include "../gpu/GpuDevice.h"
#include "../gpu/Buffer.h"
#include "../assets/ImageLoader.h"
#include "../assets/AssetResolve.h"
#include "../debug/Console.h"
#include <filesystem>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {

uint32_t findMemoryType(VkPhysicalDevice pd, uint32_t filter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((filter & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    throw std::runtime_error("Skybox: no suitable memory type");
}

// One-time layout transition covering all six cube layers.
void transitionAllLayers(VkDevice device, VkCommandPool pool, VkQueue queue, VkImage image,
                         VkImageLayout oldL, VkImageLayout newL,
                         VkAccessFlags srcA, VkAccessFlags dstA,
                         VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
    VkCommandBuffer cmd; vkAllocateCommandBuffers(device, &ai, &cmd);
    VkCommandBufferBeginInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = oldL; b.newLayout = newL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 };
    b.srcAccessMask = srcA; b.dstAccessMask = dstA;
    vkCmdPipelineBarrier(cmd, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);

    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, pool, 1, &cmd);
}

} // namespace

bool Skybox::init(GpuDevice& gpu, VkCommandPool commandPool,
                  VkDescriptorSetLayout cubeSetLayout,
                  const std::array<std::string, 6>& faces) {
    destroy(gpu.device());

    // Load all six faces (must share dimensions).
    std::array<LoadedImage, 6> imgs;
    int W = 0, H = 0;
    for (int i = 0; i < 6; ++i) {
        try {
            std::vector<uint8_t> bytes = assets::readBytes(faces[i]);
            if (bytes.empty()) throw std::runtime_error("not found");
            imgs[i] = ImageLoader::loadFromMemory(bytes.data(), static_cast<int>(bytes.size()));
        } catch (const std::exception& e) {
            Console::log(std::string("[Skybox] failed to load face: ") + faces[i], LogSeverity::Error);
            return false;
        }
        if (i == 0) { W = imgs[i].width; H = imgs[i].height; }
        else if (imgs[i].width != W || imgs[i].height != H) {
            Console::log("[Skybox] cube faces must all share the same dimensions", LogSeverity::Error);
            return false;
        }
    }

    VkDevice device = gpu.device();
    VkQueue  queue  = gpu.graphicsQueue();
    const VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;

    // Cube-compatible image with 6 array layers.
    VkImageCreateInfo ici{};
    ici.sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.flags       = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    ici.imageType   = VK_IMAGE_TYPE_2D;
    ici.format      = format;
    ici.extent      = { (uint32_t)W, (uint32_t)H, 1 };
    ici.mipLevels   = 1;
    ici.arrayLayers = 6;
    ici.samples     = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling      = VK_IMAGE_TILING_OPTIMAL;
    ici.usage       = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(device, &ici, nullptr, &image_) != VK_SUCCESS) return false;

    VkMemoryRequirements mr{}; vkGetImageMemoryRequirements(device, image_, &mr);
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = findMemoryType(gpu.physicalDevice(), mr.memoryTypeBits,
                                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device, &mai, nullptr, &memory_);
    vkBindImageMemory(device, image_, memory_, 0);

    // Stage all six faces contiguously.
    const VkDeviceSize faceBytes = (VkDeviceSize)W * H * 4;
    Buffer staging = Buffer::create(gpu, faceBytes * 6,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* mapped; vkMapMemory(device, staging.getMemory(), 0, faceBytes * 6, 0, &mapped);
    for (int i = 0; i < 6; ++i)
        std::memcpy((uint8_t*)mapped + i * faceBytes, imgs[i].pixels.data(), (size_t)faceBytes);
    vkUnmapMemory(device, staging.getMemory());

    transitionAllLayers(device, commandPool, queue, image_,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    // Copy each face into its layer.
    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = commandPool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
    VkCommandBuffer cmd; vkAllocateCommandBuffers(device, &cai, &cmd);
    VkCommandBufferBeginInfo cbi{}; cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &cbi);
    std::vector<VkBufferImageCopy> regions(6);
    for (int i = 0; i < 6; ++i) {
        regions[i] = {};
        regions[i].bufferOffset = i * faceBytes;
        regions[i].imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, (uint32_t)i, 1 };
        regions[i].imageExtent = { (uint32_t)W, (uint32_t)H, 1 };
    }
    vkCmdCopyBufferToImage(cmd, staging.get(), image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           6, regions.data());
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
    staging.destroy(device);

    transitionAllLayers(device, commandPool, queue, image_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    // Cube image view.
    VkImageViewCreateInfo vci{};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = image_;
    vci.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    vci.format = format;
    vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 };
    vkCreateImageView(device, &vci, nullptr, &view_);

    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sci.maxLod = 0.0f;
    vkCreateSampler(device, &sci, nullptr, &sampler_);

    // One-set descriptor pool + set.
    VkDescriptorPoolSize ps{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
    VkDescriptorPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.poolSizeCount = 1; pci.pPoolSizes = &ps; pci.maxSets = 1;
    vkCreateDescriptorPool(device, &pci, nullptr, &pool_);

    VkDescriptorSetAllocateInfo dai{};
    dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool = pool_; dai.descriptorSetCount = 1; dai.pSetLayouts = &cubeSetLayout;
    vkAllocateDescriptorSets(device, &dai, &descriptorSet_);

    VkDescriptorImageInfo info{};
    info.sampler = sampler_; info.imageView = view_;
    info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = descriptorSet_; w.dstBinding = 0; w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &info;
    vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);

    Console::log("[Skybox] cubemap loaded", LogSeverity::Info);
    return true;
}

void Skybox::destroy(VkDevice device) {
    if (pool_ != VK_NULL_HANDLE)    { vkDestroyDescriptorPool(device, pool_, nullptr); pool_ = VK_NULL_HANDLE; }
    if (sampler_ != VK_NULL_HANDLE) { vkDestroySampler(device, sampler_, nullptr); sampler_ = VK_NULL_HANDLE; }
    if (view_ != VK_NULL_HANDLE)    { vkDestroyImageView(device, view_, nullptr); view_ = VK_NULL_HANDLE; }
    if (image_ != VK_NULL_HANDLE)   { vkDestroyImage(device, image_, nullptr); image_ = VK_NULL_HANDLE; }
    if (memory_ != VK_NULL_HANDLE)  { vkFreeMemory(device, memory_, nullptr); memory_ = VK_NULL_HANDLE; }
    descriptorSet_ = VK_NULL_HANDLE;
}
