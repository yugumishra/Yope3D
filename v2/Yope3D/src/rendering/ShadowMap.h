#pragma once
#include <vulkan/vulkan.h>

class GpuDevice;

// ---------------------------------------------------------------------------
// ShadowMap
//
// A single fixed-resolution, sampleable depth image for the one scene shadow
// caster (see World::getShadowCaster). Independent of the swapchain — created
// once and never resized. Owns its own depth-only framebuffer against a
// RenderPass::createShadowPass render pass.
//
// The comparison sampler (compareEnable=VK_TRUE) gives free 2x2 hardware PCF
// when sampled with `sampler2DShadow` in GLSL via texture()/textureOffset().
// ---------------------------------------------------------------------------
class ShadowMap {
public:
    static constexpr uint32_t RESOLUTION = 2048;

    ShadowMap(GpuDevice& gpu, VkRenderPass shadowPass);
    ~ShadowMap();

    VkImageView   imageView()   const { return view_; }
    VkSampler     sampler()     const { return sampler_; }
    VkFramebuffer framebuffer() const { return framebuffer_; }
    VkFormat      format()      const { return format_; }
    uint32_t      resolution()  const { return RESOLUTION; }

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

private:
    VkDevice       device_      = VK_NULL_HANDLE;
    VkImage        image_       = VK_NULL_HANDLE;
    VkDeviceMemory memory_      = VK_NULL_HANDLE;
    VkImageView    view_        = VK_NULL_HANDLE;
    VkSampler      sampler_     = VK_NULL_HANDLE;
    VkFramebuffer  framebuffer_ = VK_NULL_HANDLE;
    VkFormat       format_      = VK_FORMAT_D32_SFLOAT;
};
