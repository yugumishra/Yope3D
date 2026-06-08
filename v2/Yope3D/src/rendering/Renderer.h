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
#include "gpu/Text3DBuffer.h"
#include "gpu/LineBuffer.h"
#include "Light.h"
#include "../world/World.h"
#include "RenderMode.h"

class GpuDevice;
class Window;
class Camera;
class World;
class Swapchain;
class RenderPass;
class DescriptorSetLayout;
class DescriptorPool;
class UIManager;
class Raytracer;

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

    void drawFrame(GpuDevice& gpu, Window& window, const Camera& camera, World& world,
                   class AssetManager& assets);

    void waitIdle(GpuDevice& gpu);

    void setUIManager(UIManager* mgr) { uiManager_ = mgr; }
    void setMode(RenderMode mode)    { mode_ = mode; }

    VkCommandPool         getCommandPool()      const { return commandPool; }
    VkDescriptorSetLayout getTextureSetLayout() const;
    const Swapchain&      getSwapchain()        const { return *swapchain; }
    VkFormat              getDepthFormat()      const;

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

    // 3D world-space text pipeline (recorded INSIDE the main 3D pass, depth-tested).
    VkPipelineLayout                     text3DPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline                           text3DPipeline_       = VK_NULL_HANDLE;
    std::array<Text3DBuffer, MAX_FRAMES> text3DBuffers_;
    struct Text3DDrawCall {
        uint32_t        indexCount   = 0;
        uint32_t        indexOffset  = 0;
        int32_t         vertexOffset = 0;
        VkDescriptorSet atlas        = VK_NULL_HANDLE;
        float           distanceRange = 0.0f;
        int32_t         billboard    = 0;
        math::Mat4      model;
    };
    std::vector<Text3DDrawCall> ecsText3DDrawCalls_;

    // Debug-line pipeline (GJK CSO / simplex viz). World-space LINE_LIST, no depth
    // test (always-on-top gizmo), recorded INSIDE the main 3D pass. Source data
    // comes from World::getDebugLines(), uploaded once per frame.
    VkPipelineLayout                    linePipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline                          linePipeline_       = VK_NULL_HANDLE;
    std::array<LineBuffer, MAX_FRAMES>  lineBuffers_;
    uint32_t                            lineVertexCount_    = 0;

    VkCommandPool                           commandPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, MAX_FRAMES> cmdBuffers{};

    std::array<VkSemaphore, MAX_FRAMES> imageAvailable{};
    std::vector<VkSemaphore>            renderFinished;  // one per swapchain image
    std::array<VkFence,     MAX_FRAMES> inFlightFence{};
    uint32_t currentFrame = 0;

    // Raytracer
    std::unique_ptr<Raytracer>  raytracer_;
    RenderMode                  mode_          = RenderMode::RASTER;
    std::unique_ptr<RenderPass> raytracePass_;

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

    void createRaytracePass(GpuDevice& gpu);

    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex, World& world,
                            class AssetManager& assets);

    // Build UI draw calls from ECS UITransform/UIBackground/etc. entities into buf.
    // Results are cached in ecsUIDrawCalls_ until the next frame.
    void buildECSUIGeometry(UIBuffer& buf, World& world, float sw, float sh);
    std::vector<UIDrawCall> ecsUIDrawCalls_;

    // 3D text: build glyph geometry from ECS TextLabel3D entities (before the
    // render pass), then record the draws inside the main 3D pass.
    void createText3DPipeline(VkDevice device);
    void createText3DBuffers(GpuDevice& gpu);
    void buildECSText3DGeometry(Text3DBuffer& buf, World& world);
    void recordText3D(VkCommandBuffer cmd);

    // Debug lines: upload World's line list to this frame's buffer (before the
    // pass), then record the draw inside the 3D pass.
    void createLinePipeline(VkDevice device);
    void createLineBuffers(GpuDevice& gpu);
    void uploadDebugLines(World& world);
    void recordDebugLines(VkCommandBuffer cmd);

#ifdef YOPE_EDITOR
public:
    // Editor-mode rendering: records the game pass into the ViewportTarget's offscreen texture.
    // The caller (EditorApp) then records ImGui into the same command buffer, followed by
    // endFrameForEditor which ends + submits + presents.
    // Returns the acquired swapchain image index.
    uint32_t beginFrameForEditor(GpuDevice& gpu, Window& window,
                                 const Camera& camera, World& world,
                                 class AssetManager& assets,
                                 class ViewportTarget& vt);
    // Returns true if swapchain was recreated (EditorApp must rebuild ImGui framebuffers).
    bool endFrameForEditor(GpuDevice& gpu, Window& window, uint32_t imageIndex);

    // Called by EditorApp BEFORE viewportTarget_.resize() so the framebuffer
    // that references the old color image view is destroyed first; the next
    // beginFrameForEditor lazily recreates one against the new view.
    void notifyViewportResizing(VkDevice device);

    VkCommandBuffer       currentCmdBuffer()        const { return cmdBuffers[currentFrame]; }
    VkRenderPass          getOffscreenGamePass()    const;
    VkRenderPass          getOffscreenRaytracePass()const;
    void                  notifySwapchainRecreated(GpuDevice& gpu, Window& window);
    VkDescriptorSetLayout getUBOSetLayout()         const;
    VkDescriptorSet       getCurrentDescriptorSet() const { return descriptorSets[currentFrame]; }

private:
    std::unique_ptr<RenderPass> offscreenGamePass_;
    std::unique_ptr<RenderPass> offscreenRaytracePass_;

    // Editor UI overlay: a second render pass after the offscreen game pass,
    // targeting the same color attachment so UI composites on top of the 3D
    // viewport image the developer sees in the editor panel.
    std::unique_ptr<RenderPass> offscreenUIPass_;
    // One framebuffer per frame-in-flight — the ViewportTarget is double-buffered,
    // so the color view this composites onto alternates each frame. Cached views
    // drive lazy recreation (only on resize, not every frame).
    std::array<VkFramebuffer, MAX_FRAMES> offscreenUIFb_{};
    std::array<VkImageView,   MAX_FRAMES> offscreenUIView_{};  // cached, for change detection
    uint32_t                    offscreenUIW_      = 0;
    uint32_t                    offscreenUIH_      = 0;

    void createOffscreenGamePass(GpuDevice& gpu);
    void createOffscreenRaytracePass(GpuDevice& gpu);
    void createOffscreenUIPass(GpuDevice& gpu);
    void recreateOffscreenUIFramebufferIfNeeded(VkDevice device,
                                                class ViewportTarget& vt);
    void destroyOffscreenUIFramebuffer(VkDevice device);
#endif
};
