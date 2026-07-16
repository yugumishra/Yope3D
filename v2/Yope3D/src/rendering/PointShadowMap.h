#pragma once
#include <vulkan/vulkan.h>
#include <array>

class GpuDevice;

// ---------------------------------------------------------------------------
// PointShadowMap
//
// Cube shadow map for the one scene shadow caster when it's a point light (see
// World::getShadowCaster). Implemented as a 6-layer 2D array depth image rather
// than a true VK_IMAGE_VIEW_TYPE_CUBE: the shadow pass renders each face through
// its own single-layer framebuffer (reusing the same depth-only render pass as
// the directional/spot ShadowMap), and the main pass samples it as
// sampler2DArray, picking the face by major axis in the fragment shader instead
// of relying on hardware cube sampling. This mirrors the engine's existing
// manual-PCF-over-sampler2D convention for the directional/spot map instead of
// introducing a second, cube-specific sampling path.
// ---------------------------------------------------------------------------
class PointShadowMap {
public:
    static constexpr uint32_t RESOLUTION = 1024;
    static constexpr uint32_t FACES      = 6;

    PointShadowMap(GpuDevice& gpu, VkRenderPass shadowPass);
    ~PointShadowMap();

    VkImageView   arrayView()        const { return arrayView_; }  // sampler2DArray, 6 layers
    VkFramebuffer framebuffer(int i) const { return framebuffers_[i]; }
    VkSampler     sampler()          const { return sampler_; }
    VkFormat      format()           const { return format_; }
    uint32_t      resolution()       const { return RESOLUTION; }

    PointShadowMap(const PointShadowMap&) = delete;
    PointShadowMap& operator=(const PointShadowMap&) = delete;

private:
    VkDevice       device_    = VK_NULL_HANDLE;
    VkImage        image_     = VK_NULL_HANDLE;
    VkDeviceMemory memory_    = VK_NULL_HANDLE;
    VkImageView    arrayView_ = VK_NULL_HANDLE;
    std::array<VkImageView, FACES>    faceViews_{};
    std::array<VkFramebuffer, FACES>  framebuffers_{};
    VkSampler      sampler_   = VK_NULL_HANDLE;
    VkFormat       format_    = VK_FORMAT_D32_SFLOAT;
};
