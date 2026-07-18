#pragma once
#ifdef YOPE_EDITOR
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>

class GpuDevice;

// Offscreen render target for the editor viewport panel.
// The game renders into this instead of the swapchain; ImGui::Image then
// displays it inside the Viewport panel.
//
// Double-buffered: holds one color/depth image set per frame-in-flight so the
// editor can keep two frames in flight without a read/write race on a shared
// image. Frame N's ImGui pass samples its own color image while frame N+1 is
// already rendering into a different one. The renderer selects the slot for the
// current frame via setActiveFrame(); every accessor returns that slot's
// resource. (In edit mode the scene is static so the race was invisible, but it
// produced visible flicker once the scene started animating in play mode.)
class ViewportTarget {
public:
    // Must match Renderer::MAX_FRAMES.
    static constexpr uint32_t kFrames = 2;

    ViewportTarget() = default;
    ViewportTarget(GpuDevice& gpu,
                   VkRenderPass offscreenGamePass,
                   VkRenderPass offscreenRaytracePass,
                   VkFormat colorFormat,
                   VkFormat depthFormat,
                   uint32_t w, uint32_t h);

    void resize(GpuDevice& gpu, uint32_t w, uint32_t h);
    void destroy(VkDevice device);

    // Select the frame-in-flight slot the accessors below resolve to. Called by
    // the renderer at the start of beginFrameForEditor with its currentFrame.
    void setActiveFrame(uint32_t i) { activeFrame_ = i % kFrames; }

    VkFramebuffer   framebuffer()         const { return frames_[activeFrame_].framebuffer; }
    VkFramebuffer   raytraceFramebuffer() const { return frames_[activeFrame_].raytraceFramebuffer; }
    VkImage         colorImage()          const { return frames_[activeFrame_].colorImage; }
    VkImageView     colorView()           const { return frames_[activeFrame_].colorView; }
    VkImageView     depthView()           const { return frames_[activeFrame_].depthView; }
    VkDescriptorSet imguiDescriptor()     const { return frames_[activeFrame_].imguiDescSet; }
    uint32_t        width()               const { return w_; }
    uint32_t        height()              const { return h_; }

    ViewportTarget(const ViewportTarget&)            = delete;
    ViewportTarget& operator=(const ViewportTarget&) = delete;
    ViewportTarget(ViewportTarget&& o) noexcept;
    ViewportTarget& operator=(ViewportTarget&& o) noexcept;

private:
    void create(GpuDevice& gpu, VkRenderPass offscreenGamePass, VkRenderPass offscreenRaytracePass,
                VkFormat colorFormat, VkFormat depthFormat, uint32_t w, uint32_t h);

    // Per-frame-in-flight resources. The sampler is shared (see below).
    struct Frame {
        VkImage         colorImage          = VK_NULL_HANDLE;
        VkDeviceMemory  colorMem            = VK_NULL_HANDLE;
        VkImageView     colorView           = VK_NULL_HANDLE;
        VkImage         depthImage          = VK_NULL_HANDLE;
        VkDeviceMemory  depthMem            = VK_NULL_HANDLE;
        VkImageView     depthView           = VK_NULL_HANDLE;
        VkFramebuffer   framebuffer         = VK_NULL_HANDLE;
        VkFramebuffer   raytraceFramebuffer = VK_NULL_HANDLE;  // color-only, for raytrace pass
        VkDescriptorSet imguiDescSet        = VK_NULL_HANDLE;
    };

    VkDevice     device_       = VK_NULL_HANDLE;
    VkRenderPass gamePass_     = VK_NULL_HANDLE;  // stored for resize
    VkRenderPass raytracePass_ = VK_NULL_HANDLE;  // non-owning ref
    VkFormat     colorFormat_  = VK_FORMAT_UNDEFINED;
    VkFormat     depthFormat_  = VK_FORMAT_UNDEFINED;

    VkSampler              sampler_ = VK_NULL_HANDLE;  // shared across frames
    std::array<Frame, kFrames> frames_{};
    uint32_t               activeFrame_ = 0;

    uint32_t w_ = 0, h_ = 0;
};
#endif
