#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <string>

class GpuDevice;

// ---------------------------------------------------------------------------
// Skybox — a cubemap texture + its descriptor set, rendered as a background.
//
// init() loads six face images (order +X,-X,+Y,-Y,+Z,-Z, asset-relative paths),
// uploads them into a VK_IMAGE_VIEW_TYPE_CUBE image, and allocates a single
// combined-image-sampler descriptor set against `cubeSetLayout` (the engine's
// reusable single-sampler set-1 layout — a samplerCube is still a combined
// image sampler). Owns its own one-set descriptor pool.
//
// The Renderer binds descriptorSet() at set 1 of the skybox pipeline.
// ---------------------------------------------------------------------------

class Skybox {
public:
    // Returns false (and leaves valid()==false) if any face fails to load.
    bool init(GpuDevice& gpu, VkCommandPool commandPool,
              VkDescriptorSetLayout cubeSetLayout,
              const std::array<std::string, 6>& faces);
    void destroy(VkDevice device);

    bool            valid()         const { return image_ != VK_NULL_HANDLE; }
    VkDescriptorSet descriptorSet() const { return descriptorSet_; }

private:
    VkImage          image_          = VK_NULL_HANDLE;
    VkDeviceMemory   memory_         = VK_NULL_HANDLE;
    VkImageView      view_           = VK_NULL_HANDLE;
    VkSampler        sampler_        = VK_NULL_HANDLE;
    VkDescriptorPool pool_           = VK_NULL_HANDLE;
    VkDescriptorSet  descriptorSet_  = VK_NULL_HANDLE;
};
