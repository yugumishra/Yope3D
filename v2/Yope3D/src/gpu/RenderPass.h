#pragma once
#include <vulkan/vulkan.h>

class RenderPass {
public:
    RenderPass(VkDevice device, VkFormat colorFormat, VkFormat depthFormat);
    ~RenderPass();

    VkRenderPass get() const { return renderPass; }

    RenderPass(const RenderPass&) = delete;
    RenderPass& operator=(const RenderPass&) = delete;

private:
    VkDevice     device     = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
};
