#pragma once
#include <vulkan/vulkan.h>
#include <vector>

// ---------------------------------------------------------------------------
// DescriptorSetLayout
//
// RAII wrapper around VkDescriptorSetLayout.
// Pass an array of binding descriptions; the layout is destroyed in the dtor.
// ---------------------------------------------------------------------------

class DescriptorSetLayout {
public:
    DescriptorSetLayout(VkDevice device,
                        const std::vector<VkDescriptorSetLayoutBinding>& bindings);
    ~DescriptorSetLayout();

    VkDescriptorSetLayout get() const { return layout; }

    DescriptorSetLayout(const DescriptorSetLayout&) = delete;
    DescriptorSetLayout& operator=(const DescriptorSetLayout&) = delete;

private:
    VkDevice              device = VK_NULL_HANDLE;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
};
