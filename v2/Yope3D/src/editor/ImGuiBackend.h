#pragma once
#ifdef YOPE_EDITOR
#include <vulkan/vulkan.h>
#include <vector>

class GpuDevice;
class Window;
class Swapchain;
class EditorTheme;

// Owns all ImGui Vulkan/GLFW state that must stay separate from Renderer.
// Renderer remains editor-agnostic; ImGuiBackend handles its own descriptor pool,
// render pass, and per-swapchain-image framebuffers.
class ImGuiBackend {
public:
    // imguiPass must have been created with createImGuiPass().
    void init(GpuDevice& gpu, Window& window, VkRenderPass imguiPass,
              uint32_t imageCount, const std::vector<VkImageView>& swapViews,
              VkExtent2D extent, VkCommandPool pool, EditorTheme& theme);

    void newFrame();

    // Records ImGui draw data into cmd using the ImGui render pass.
    void render(VkCommandBuffer cmd, uint32_t imageIndex);

    // Rebuilds per-swapchain-image framebuffers after a resize.
    void onSwapchainRecreate(VkDevice device, const std::vector<VkImageView>& views,
                             VkExtent2D extent);

    void cleanup(VkDevice device);

    VkDescriptorPool descriptorPool() const { return descPool_; }

private:
    void createFramebuffers(VkDevice device, const std::vector<VkImageView>& views,
                            VkExtent2D extent);
    void destroyFramebuffers(VkDevice device);

    VkDescriptorPool      descPool_   = VK_NULL_HANDLE;
    VkRenderPass          renderPass_ = VK_NULL_HANDLE;  // non-owning ref (owned by Renderer)
    std::vector<VkFramebuffer> framebuffers_;
    VkExtent2D            extent_     = {};
};
#endif
