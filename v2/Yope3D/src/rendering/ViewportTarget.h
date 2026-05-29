#pragma once
#ifdef YOPE_EDITOR
#include <vulkan/vulkan.h>
#include <cstdint>

class GpuDevice;

// Offscreen render target for the editor viewport panel.
// The game renders into this instead of the swapchain; ImGui::Image then
// displays it inside the Viewport panel.
class ViewportTarget {
public:
    ViewportTarget() = default;
    ViewportTarget(GpuDevice& gpu,
                   VkRenderPass offscreenGamePass,
                   VkRenderPass offscreenRaytracePass,
                   VkFormat colorFormat,
                   VkFormat depthFormat,
                   uint32_t w, uint32_t h);

    void resize(GpuDevice& gpu, uint32_t w, uint32_t h);
    void destroy(VkDevice device);

    VkFramebuffer   framebuffer()         const { return framebuffer_; }
    VkFramebuffer   raytraceFramebuffer() const { return raytraceFramebuffer_; }
    VkImageView     colorView()           const { return colorView_; }
    VkDescriptorSet imguiDescriptor()     const { return imguiDescSet_; }
    uint32_t        width()               const { return w_; }
    uint32_t        height()              const { return h_; }

    ViewportTarget(const ViewportTarget&)            = delete;
    ViewportTarget& operator=(const ViewportTarget&) = delete;
    ViewportTarget(ViewportTarget&& o) noexcept;
    ViewportTarget& operator=(ViewportTarget&& o) noexcept;

private:
    void create(GpuDevice& gpu, VkRenderPass offscreenGamePass,
                VkFormat colorFormat, VkFormat depthFormat, uint32_t w, uint32_t h);
    void create(GpuDevice& gpu, VkRenderPass offscreenGamePass, VkRenderPass offscreenRaytracePass,
                VkFormat colorFormat, VkFormat depthFormat, uint32_t w, uint32_t h);

    VkDevice device_       = VK_NULL_HANDLE;
    VkRenderPass gamePass_ = VK_NULL_HANDLE;  // stored for resize
    VkFormat colorFormat_  = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat_  = VK_FORMAT_UNDEFINED;

    VkImage        colorImage_   = VK_NULL_HANDLE;
    VkDeviceMemory colorMem_     = VK_NULL_HANDLE;
    VkImageView    colorView_    = VK_NULL_HANDLE;
    VkSampler      sampler_      = VK_NULL_HANDLE;

    VkImage        depthImage_   = VK_NULL_HANDLE;
    VkDeviceMemory depthMem_     = VK_NULL_HANDLE;
    VkImageView    depthView_    = VK_NULL_HANDLE;

    VkFramebuffer   framebuffer_         = VK_NULL_HANDLE;
    VkFramebuffer   raytraceFramebuffer_ = VK_NULL_HANDLE;  // color-only, for raytrace pass
    VkRenderPass    raytracePass_        = VK_NULL_HANDLE;  // non-owning ref
    VkDescriptorSet imguiDescSet_        = VK_NULL_HANDLE;

    uint32_t w_ = 0, h_ = 0;
};
#endif
