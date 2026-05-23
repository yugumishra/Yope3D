#pragma once
#include <vulkan/vulkan.h>

class RenderPass {
public:
    // 3D render pass: clears color + depth, finalLayout = PRESENT_SRC_KHR.
    RenderPass(VkDevice device, VkFormat colorFormat, VkFormat depthFormat);

    // UI render pass: loads (preserves) existing color, no depth attachment.
    // initialLayout = PRESENT_SRC_KHR (left by the 3D pass), finalLayout = PRESENT_SRC_KHR.
    static RenderPass createUIPass(VkDevice device, VkFormat colorFormat);

    // Raytrace render pass: clears color (no depth), finalLayout = PRESENT_SRC_KHR.
    // Used in RAYTRACE mode instead of the 3D pass.
    static RenderPass createRaytracePass(VkDevice device, VkFormat colorFormat);

    ~RenderPass();

    VkRenderPass get() const { return renderPass; }

    RenderPass(RenderPass&& o) noexcept : device(o.device), renderPass(o.renderPass) {
        o.device = VK_NULL_HANDLE; o.renderPass = VK_NULL_HANDLE;
    }
    RenderPass& operator=(RenderPass&& o) noexcept {
        device = o.device; renderPass = o.renderPass;
        o.device = VK_NULL_HANDLE; o.renderPass = VK_NULL_HANDLE;
        return *this;
    }
    RenderPass(const RenderPass&) = delete;
    RenderPass& operator=(const RenderPass&) = delete;

private:
    // Private constructor used by the createUIPass factory.
    RenderPass(VkDevice dev, VkRenderPass rp) : device(dev), renderPass(rp) {}

    VkDevice     device     = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
};
