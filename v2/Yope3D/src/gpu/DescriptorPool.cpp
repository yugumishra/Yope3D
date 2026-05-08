#include "DescriptorPool.h"
#include <stdexcept>

DescriptorPool::DescriptorPool(VkDevice device,
    const std::vector<VkDescriptorPoolSize>& poolSizes,
    uint32_t maxSets)
    : device(device)
{
    VkDescriptorPoolCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    ci.pPoolSizes    = poolSizes.data();
    ci.maxSets       = maxSets;
    if (vkCreateDescriptorPool(device, &ci, nullptr, &pool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create descriptor pool");
}

DescriptorPool::~DescriptorPool() {
    if (pool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device, pool, nullptr);
}

VkDescriptorSet DescriptorPool::allocate(VkDescriptorSetLayout layout) {
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &layout;
    VkDescriptorSet set;
    if (vkAllocateDescriptorSets(device, &ai, &set) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate descriptor set");
    return set;
}
