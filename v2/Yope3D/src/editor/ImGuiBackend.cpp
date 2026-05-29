#ifdef YOPE_EDITOR
#include "editor/ImGuiBackend.h"
#include "editor/EditorTheme.h"
#include "gpu/GpuDevice.h"
#include "platform/Window.h"
#include "gpu/Swapchain.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <array>

void ImGuiBackend::init(GpuDevice& gpu, Window& window, VkRenderPass imguiPass,
                        uint32_t imageCount, const std::vector<VkImageView>& swapViews,
                        VkExtent2D extent, VkCommandPool /*pool*/, EditorTheme& theme) {
    renderPass_ = imguiPass;
    extent_     = extent;

    // Descriptor pool — separate from Renderer's pool.
    // ImGui needs FREE_DESCRIPTOR_SET_BIT for AddTexture/RemoveTexture.
    static const std::array<VkDescriptorPoolSize, 1> poolSizes{{
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024 }
    }};
    VkDescriptorPoolCreateInfo pi{};
    pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pi.maxSets       = 1024;
    pi.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    pi.pPoolSizes    = poolSizes.data();
    if (vkCreateDescriptorPool(gpu.device(), &pi, nullptr, &descPool_) != VK_SUCCESS)
        throw std::runtime_error("ImGuiBackend: failed to create descriptor pool");

    // ImGui context + style
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Apply custom theme (fonts + colors)
    theme.apply();

    // GLFW backend
    ImGui_ImplGlfw_InitForVulkan(window.getHandle(), true);

    // Vulkan backend — 1.92.7 API: RenderPass goes in PipelineInfoMain
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion    = VK_API_VERSION_1_2;
    info.Instance      = gpu.instance();
    info.PhysicalDevice = gpu.physicalDevice();
    info.Device        = gpu.device();
    info.QueueFamily   = gpu.queueFamilies().graphics.value();
    info.Queue         = gpu.graphicsQueue();
    info.DescriptorPool = descPool_;
    info.MinImageCount = 2;
    info.ImageCount    = imageCount;
    info.PipelineInfoMain.RenderPass = imguiPass;
    ImGui_ImplVulkan_Init(&info);

    // Per-swapchain framebuffers
    createFramebuffers(gpu.device(), swapViews, extent);
}

void ImGuiBackend::newFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiBackend::render(VkCommandBuffer cmd, uint32_t imageIndex) {
    ImGui::Render();

    VkClearValue clear{};
    clear.color = {0.12f, 0.12f, 0.14f, 1.0f};

    VkRenderPassBeginInfo rpbi{};
    rpbi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass        = renderPass_;
    rpbi.framebuffer       = framebuffers_[imageIndex];
    rpbi.renderArea.offset = {0, 0};
    rpbi.renderArea.extent = extent_;
    rpbi.clearValueCount   = 1;
    rpbi.pClearValues      = &clear;

    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRenderPass(cmd);
}

void ImGuiBackend::onSwapchainRecreate(VkDevice device, const std::vector<VkImageView>& views,
                                       VkExtent2D extent) {
    extent_ = extent;
    destroyFramebuffers(device);
    createFramebuffers(device, views, extent);
    ImGui_ImplVulkan_SetMinImageCount(2);
}

void ImGuiBackend::cleanup(VkDevice device) {
    destroyFramebuffers(device);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (descPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descPool_, nullptr);
        descPool_ = VK_NULL_HANDLE;
    }
}

void ImGuiBackend::createFramebuffers(VkDevice device, const std::vector<VkImageView>& views,
                                      VkExtent2D extent) {
    framebuffers_.resize(views.size());
    for (size_t i = 0; i < views.size(); ++i) {
        VkFramebufferCreateInfo fbi{};
        fbi.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbi.renderPass      = renderPass_;
        fbi.attachmentCount = 1;
        fbi.pAttachments    = &views[i];
        fbi.width           = extent.width;
        fbi.height          = extent.height;
        fbi.layers          = 1;
        if (vkCreateFramebuffer(device, &fbi, nullptr, &framebuffers_[i]) != VK_SUCCESS)
            throw std::runtime_error("ImGuiBackend: failed to create framebuffer");
    }
}

void ImGuiBackend::destroyFramebuffers(VkDevice device) {
    for (auto fb : framebuffers_) vkDestroyFramebuffer(device, fb, nullptr);
    framebuffers_.clear();
}
#endif
