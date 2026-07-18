#pragma once
#include <vulkan/vulkan.h>
#include <vector>

// ---------------------------------------------------------------------------
// DescriptorPool
//
// RAII wrapper around VkDescriptorPool.
// Call allocate() to hand out individual VkDescriptorSets.
// ---------------------------------------------------------------------------

class DescriptorPool {
public:
    // poolSizes: how many descriptors of each type to reserve.
    // maxSets:   maximum total descriptor sets allocatable from this pool.
    DescriptorPool(VkDevice device,
                   const std::vector<VkDescriptorPoolSize>& poolSizes,
                   uint32_t maxSets);
    ~DescriptorPool();

    VkDescriptorSet  allocate(VkDescriptorSetLayout layout);
    VkDescriptorPool get()    const { return pool; }

    DescriptorPool(const DescriptorPool&) = delete;
    DescriptorPool& operator=(const DescriptorPool&) = delete;

private:
    VkDevice         device = VK_NULL_HANDLE;
    VkDescriptorPool pool   = VK_NULL_HANDLE;
};
