#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <array>
#include "../world/RenderMesh.h"
#include "gpu/UniformBuffer.h"
#include "gpu/StorageBuffer.h"
#include "gpu/DepthBuffer.h"
#include "gpu/UIBuffer.h"
#include "Light.h"
#include "../world/World.h"

class GpuDevice;
class Window;
class Camera;
class World;
class Swapchain;
class RenderPass;
class DescriptorSetLayout;
class DescriptorPool;
class UIManager;

// ---------------------------------------------------------------------------
// Renderer
//
// Owns everything between the logical device and the screen:
//   - Swapchain + image views
//   - Render pass
//   - Descriptor set layout + pool + per-frame sets (binding 0 → GlobalUBO)
//   - Per-frame UniformBuffers (view, proj, cameraPos — written before draw)
//   - Graphics pipeline (push constants carry the per-object model matrix)
//   - Framebuffers (one per swapchain image, rebuilt on resize)
//   - Command pool + per-frame command buffers
//   - Synchronisation objects (2 frames in flight)
//   - Iterates over World's RenderMeshes for rendering
// ---------------------------------------------------------------------------

class Renderer {
public:
    Renderer(GpuDevice& gpu, Window& window);
    ~Renderer();

    void drawFrame(GpuDevice& gpu, Window& window, const Camera& camera, const World& world,
                   class AssetManager& assets);
    void waitIdle(GpuDevice& gpu);

    void setUIManager(UIManager* mgr) { uiManager_ = mgr; }

    VkCommandPool         getCommandPool()      const { return commandPool; }
    VkDescriptorSetLayout getTextureSetLayout() const;

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

private:
    static constexpr int MAX_FRAMES = 2;

    std::unique_ptr<Swapchain>           swapchain;
    std::unique_ptr<RenderPass>          renderPass;
    std::unique_ptr<DepthBuffer>         depthBuffer;
    std::unique_ptr<DescriptorSetLayout> uboLayout;
    std::unique_ptr<DescriptorSetLayout> textureSetLayout;
    std::unique_ptr<DescriptorPool>      descriptorPool;

    std::array<UniformBuffer,    MAX_FRAMES> uniformBuffers;
    std::array<StorageBuffer,    MAX_FRAMES> lightBuffers;
    std::array<VkDescriptorSet,  MAX_FRAMES> descriptorSets{};

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline       pipeline       = VK_NULL_HANDLE;

    std::vector<VkFramebuffer> framebuffers;

    // UI pipeline (separate render pass after the 3D pass)
    std::unique_ptr<RenderPass>       uiRenderPass;
    std::vector<VkFramebuffer>        uiFramebuffers;
    VkPipelineLayout                  uiPipelineLayout = VK_NULL_HANDLE;
    VkPipeline                        uiPipeline       = VK_NULL_HANDLE;
    std::array<UIBuffer, MAX_FRAMES>  uiBuffers;
    UIManager*                        uiManager_       = nullptr;

    VkCommandPool                           commandPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, MAX_FRAMES> cmdBuffers{};

    std::array<VkSemaphore, MAX_FRAMES> imageAvailable{};
    std::vector<VkSemaphore>            renderFinished;  // one per swapchain image
    std::array<VkFence,     MAX_FRAMES> inFlightFence{};
    uint32_t currentFrame = 0;

    void createRenderPass(GpuDevice& gpu);
    void createUBOLayout(VkDevice device);
    void createTextureSetLayout(VkDevice device);
    void createUniformBuffers(GpuDevice& gpu);
    void createDescriptorPool(VkDevice device);
    void createDescriptorSets(VkDevice device);
    void createPipeline(VkDevice device);
    void createFramebuffers(VkDevice device);
    void createCommandPool(GpuDevice& gpu);
    void allocateCommandBuffers(VkDevice device);
    void createSyncObjects(VkDevice device);
    void createLightBuffers(GpuDevice& gpu);

    void recreateSwapchain(GpuDevice& gpu, Window& window);
    void destroyFramebuffers(VkDevice device);
    void destroyUIFramebuffers(VkDevice device);

    void createUIRenderPass(GpuDevice& gpu);
    void createUIFramebuffers(VkDevice device);
    void createUIPipeline(VkDevice device);
    void createUIBuffers(GpuDevice& gpu);

    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex, const World& world,
                            class AssetManager& assets);
};
