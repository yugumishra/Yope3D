#include "DescriptorSetLayout.h"
#include <stdexcept>

DescriptorSetLayout::DescriptorSetLayout(VkDevice device,
    const std::vector<VkDescriptorSetLayoutBinding>& bindings)
    : device(device)
{
    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = static_cast<uint32_t>(bindings.size());
    ci.pBindings    = bindings.data();
    if (vkCreateDescriptorSetLayout(device, &ci, nullptr, &layout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create descriptor set layout");
}

DescriptorSetLayout::~DescriptorSetLayout() {
    if (layout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device, layout, nullptr);
}
