#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include "gpu/StorageImage.h"
#include "gpu/StorageBuffer.h"
#include "gpu/ComputePipeline.h"

class GpuDevice;
class Swapchain;
class World;

// ---------------------------------------------------------------------------
// Raytracer — owns all GPU state specific to the compute-shader raytrace path.
//
// Per-frame flow (driven by Renderer):
//   prepareFrame()   — pack world geometry into geometry SSBO
//   dispatch()       — compute dispatch + barrier (outside any render pass)
//   recordBlit()     — fullscreen-triangle draw (inside raytrace render pass)
// ---------------------------------------------------------------------------

class Raytracer {
public:
    // maxGeometryFloats: upper bound on packed geometry floats per frame.
    static constexpr uint32_t MAX_GEOMETRY_FLOATS = 131072;  // 128 K — fits 400 boxes (6 quads each) with headroom
    static constexpr int      MAX_FRAMES          = 2;

    Raytracer(GpuDevice& gpu,
              VkCommandPool         cmdPool,
              const Swapchain&      swapchain,
              VkRenderPass          raytraceRenderPass,
              // Per-frame shared resources from Renderer (same VkBuffer, no copy):
              std::array<VkBuffer, 2> uboBuffers,
              std::array<VkDeviceSize, 2> uboSizes,
              std::array<VkBuffer, 2> lightBuffers,
              std::array<VkDeviceSize, 2> lightSizes);

    ~Raytracer();

    Raytracer(const Raytracer&) = delete;
    Raytracer& operator=(const Raytracer&) = delete;

    // Recreate size-dependent resources after window resize.
    void onResize(GpuDevice& gpu, uint32_t newWidth, uint32_t newHeight, VkCommandPool cmdPool);

    // Pack world geometry into this frame's SSBO. Call before dispatch().
    void prepareFrame(uint32_t frameIndex, const World& world);

    // Record compute dispatch + barriers into cmd (outside any render pass).
    void dispatch(VkCommandBuffer cmd, uint32_t frameIndex);

    // Record the fullscreen-triangle draw inside the raytrace render pass.
    void recordBlit(VkCommandBuffer cmd, uint32_t frameIndex);

    std::vector<VkFramebuffer> framebuffers; // one per swapchain image, color-only

private:
    GpuDevice*       gpu_;
    const Swapchain* swapchain_;
    VkRenderPass     raytracePass_;

    std::array<StorageImage,  MAX_FRAMES> storageImages_;
    std::array<StorageBuffer, MAX_FRAMES> geometryBuffers_;

    // --- Compute pipeline ---
    VkDescriptorSetLayout         computeSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool              computePool_       = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MAX_FRAMES> computeSets_{};
    VkPipelineLayout              computePipeLayout_ = VK_NULL_HANDLE;
    ComputePipeline               computePipeline_;

    // --- Fullscreen blit pipeline ---
    VkDescriptorSetLayout         blitSetLayout_  = VK_NULL_HANDLE;
    VkDescriptorPool              blitPool_        = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MAX_FRAMES> blitSets_{};
    VkPipelineLayout              blitPipeLayout_ = VK_NULL_HANDLE;
    VkPipeline                    blitPipeline_   = VK_NULL_HANDLE;

    // Shared UBO + light references (same VkBuffers, no ownership)
    std::array<VkBuffer,     MAX_FRAMES> uboBuffers_;
    std::array<VkDeviceSize, MAX_FRAMES> uboSizes_;
    std::array<VkBuffer,     MAX_FRAMES> lightBuffers_;
    std::array<VkDeviceSize, MAX_FRAMES> lightSizes_;

    void createComputeLayout(VkDevice device);
    void createComputePool(VkDevice device);
    void createComputePipeline(VkDevice device, VkCommandPool cmdPool);
    void createBlitLayout(VkDevice device);
    void createBlitPool(VkDevice device);
    void createBlitPipeline(GpuDevice& gpu);
    void createFramebuffers(VkDevice device);
    void destroyFramebuffers(VkDevice device);
    void writeComputeDescriptors(VkDevice device, uint32_t frameIndex);
    void writeBlitDescriptors(VkDevice device, uint32_t frameIndex);
    void packGeometry(const World& world, std::vector<float>& out);
};
