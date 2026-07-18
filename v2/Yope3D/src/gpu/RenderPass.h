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

    // Offscreen game pass (YOPE_EDITOR): identical to the 3D pass constructor but
    // finalLayout = SHADER_READ_ONLY_OPTIMAL so ImGui can sample the texture directly.
    static RenderPass createOffscreenGamePass(VkDevice device, VkFormat colorFormat,
                                              VkFormat depthFormat);

    // ImGui pass (YOPE_EDITOR): clears the swapchain surface and renders ImGui on top.
    static RenderPass createImGuiPass(VkDevice device, VkFormat colorFormat);

    // Raytrace offscreen pass (YOPE_EDITOR): color-only (no depth), clears on load,
    // finalLayout = SHADER_READ_ONLY_OPTIMAL so ImGui can sample the result.
    static RenderPass createOffscreenRaytracePass(VkDevice device, VkFormat colorFormat);

    // Shadow depth pass: single depth-only attachment, clears each frame, finalLayout
    // = DEPTH_STENCIL_READ_ONLY_OPTIMAL so the main pass can sample it directly. The
    // shadow map is a persistent image reused every frame (like the main depth
    // buffer) — dependencies guard both the previous frame's sampled-read before this
    // frame's write, and this frame's write before the main pass samples it.
    static RenderPass createShadowPass(VkDevice device, VkFormat depthFormat);

    // UI offscreen pass (YOPE_EDITOR): loads existing color (preserves the 3D
    // game pass underneath), no depth attachment, initialLayout and finalLayout
    // both SHADER_READ_ONLY_OPTIMAL so the same texture stays consumable by ImGui.
    static RenderPass createOffscreenUIPass(VkDevice device, VkFormat colorFormat);

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
