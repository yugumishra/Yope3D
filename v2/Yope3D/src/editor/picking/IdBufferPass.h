#pragma once
#ifdef YOPE_EDITOR
#include <vulkan/vulkan.h>
#include <cstdint>

class GpuDevice;
namespace ecs { class Registry; class Entity; }
class Selection;

// Renders all EditorPickable entities with their entity ID as pixel color (R32_UINT).
// On viewport click, reads back the pixel to resolve the entity under cursor.
class IdBufferPass {
public:
    IdBufferPass() = default;
    ~IdBufferPass() = default;

    // Call once. uboSetLayout + descriptorSet come from Renderer.
    void init(GpuDevice& gpu,
              VkDescriptorSetLayout uboSetLayout,
              VkCommandPool         cmdPool,
              VkFormat              depthFormat,
              uint32_t w, uint32_t h);

    void resize(GpuDevice& gpu, VkImageView sharedDepthView, uint32_t w, uint32_t h);
    void destroy(VkDevice device);

    // Record the ID pass + optional readback copy into cmd.
    void record(VkCommandBuffer cmd, ecs::Registry& reg,
                VkDescriptorSet globalDescSet,
                VkImageView sharedDepthView,
                uint32_t vpW, uint32_t vpH);

    // Schedule a readback at the given viewport coordinates.
    void scheduleReadback(uint32_t x, uint32_t y);

    // Call each frame before panel draws.
    // If a readback was scheduled last frame and is ready, updates selection.
    void pollResult(ecs::Registry& reg, Selection& sel);

private:
    void createIdImage(GpuDevice& gpu, uint32_t w, uint32_t h);
    void createRenderPass(VkDevice device, VkFormat depthFormat);
    void createFramebuffer(VkDevice device, VkImageView sharedDepthView, uint32_t w, uint32_t h);
    void createPipeline(VkDevice device, VkDescriptorSetLayout uboSetLayout);
    void createStagingBuffer(GpuDevice& gpu);
    void destroyIdImageResources(VkDevice device);

    // ID image (R32_UINT)
    VkImage        idImage_    = VK_NULL_HANDLE;
    VkDeviceMemory idMem_      = VK_NULL_HANDLE;
    VkImageView    idView_     = VK_NULL_HANDLE;

    VkRenderPass   renderPass_ = VK_NULL_HANDLE;
    VkFramebuffer  framebuffer_= VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       pipeline_       = VK_NULL_HANDLE;

    // 1-pixel host-visible staging buffer for readback
    VkBuffer       stagingBuf_  = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem_  = VK_NULL_HANDLE;

    VkDevice device_ = VK_NULL_HANDLE;

    uint32_t w_ = 0, h_ = 0;
    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;

    // Readback state
    bool     pendingReadback_ = false;
    uint32_t clickX_ = 0, clickY_ = 0;
    bool     readbackReady_   = false;   // set after the frame that scheduled the copy
};
#endif
