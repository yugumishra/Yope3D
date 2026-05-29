#ifdef YOPE_EDITOR
#include "ViewportTarget.h"
#include "gpu/GpuDevice.h"
#include <imgui_impl_vulkan.h>
#include <stdexcept>

ViewportTarget::ViewportTarget(GpuDevice& gpu,
                               VkRenderPass offscreenGamePass,
                               VkRenderPass offscreenRaytracePass,
                               VkFormat colorFormat,
                               VkFormat depthFormat,
                               uint32_t w, uint32_t h) {
    create(gpu, offscreenGamePass, offscreenRaytracePass, colorFormat, depthFormat, w, h);
}

void ViewportTarget::create(GpuDevice& gpu, VkRenderPass offscreenGamePass,
                            VkFormat colorFormat, VkFormat depthFormat,
                            uint32_t w, uint32_t h) {
    create(gpu, offscreenGamePass, VK_NULL_HANDLE, colorFormat, depthFormat, w, h);
}

void ViewportTarget::create(GpuDevice& gpu, VkRenderPass offscreenGamePass,
                            VkRenderPass offscreenRaytracePass,
                            VkFormat colorFormat, VkFormat depthFormat,
                            uint32_t w, uint32_t h) {
    device_       = gpu.device();
    gamePass_     = offscreenGamePass;
    raytracePass_ = offscreenRaytracePass;
    colorFormat_  = colorFormat;
    depthFormat_  = depthFormat;
    w_ = w;  h_ = h;

    auto allocImage = [&](VkFormat fmt, VkImageUsageFlags usage,
                          VkImage& img, VkDeviceMemory& mem) {
        VkImageCreateInfo ii{};
        ii.sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType   = VK_IMAGE_TYPE_2D;
        ii.format      = fmt;
        ii.extent      = {w, h, 1};
        ii.mipLevels   = 1;
        ii.arrayLayers = 1;
        ii.samples     = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling      = VK_IMAGE_TILING_OPTIMAL;
        ii.usage       = usage;
        ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device_, &ii, nullptr, &img) != VK_SUCCESS)
            throw std::runtime_error("ViewportTarget: failed to create image");
        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(device_, img, &mr);
        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = mr.size;
        ai.memoryTypeIndex = gpu.findMemoryType(mr.memoryTypeBits,
                                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(device_, &ai, nullptr, &mem) != VK_SUCCESS)
            throw std::runtime_error("ViewportTarget: failed to allocate image memory");
        vkBindImageMemory(device_, img, mem, 0);
    };

    // Color image (sampled by ImGui::Image)
    allocImage(colorFormat_,
               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
               colorImage_, colorMem_);

    VkImageViewCreateInfo cvi{};
    cvi.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    cvi.image    = colorImage_;
    cvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    cvi.format   = colorFormat_;
    cvi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device_, &cvi, nullptr, &colorView_) != VK_SUCCESS)
        throw std::runtime_error("ViewportTarget: failed to create color image view");

    // Depth image
    allocImage(depthFormat_,
               VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
               depthImage_, depthMem_);

    VkImageViewCreateInfo dvi{};
    dvi.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    dvi.image    = depthImage_;
    dvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    dvi.format   = depthFormat_;
    dvi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device_, &dvi, nullptr, &depthView_) != VK_SUCCESS)
        throw std::runtime_error("ViewportTarget: failed to create depth image view");

    // Sampler for ImGui::Image
    VkSamplerCreateInfo si{};
    si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter    = VK_FILTER_LINEAR;
    si.minFilter    = VK_FILTER_LINEAR;
    si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.minLod = 0.0f;  si.maxLod = 0.0f;
    if (vkCreateSampler(device_, &si, nullptr, &sampler_) != VK_SUCCESS)
        throw std::runtime_error("ViewportTarget: failed to create sampler");

    // Framebuffer (against the offscreen game pass)
    VkImageView attachments[2] = {colorView_, depthView_};
    VkFramebufferCreateInfo fbi{};
    fbi.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbi.renderPass      = gamePass_;
    fbi.attachmentCount = 2;
    fbi.pAttachments    = attachments;
    fbi.width           = w_;
    fbi.height          = h_;
    fbi.layers          = 1;
    if (vkCreateFramebuffer(device_, &fbi, nullptr, &framebuffer_) != VK_SUCCESS)
        throw std::runtime_error("ViewportTarget: failed to create framebuffer");

    // Color-only framebuffer for raytrace pass (no depth attachment)
    if (raytracePass_ != VK_NULL_HANDLE) {
        VkFramebufferCreateInfo rtfbi{};
        rtfbi.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        rtfbi.renderPass      = raytracePass_;
        rtfbi.attachmentCount = 1;
        rtfbi.pAttachments    = &colorView_;
        rtfbi.width           = w_;
        rtfbi.height          = h_;
        rtfbi.layers          = 1;
        if (vkCreateFramebuffer(device_, &rtfbi, nullptr, &raytraceFramebuffer_) != VK_SUCCESS)
            throw std::runtime_error("ViewportTarget: failed to create raytrace framebuffer");
    }

    // ImGui descriptor set for ImGui::Image()
    imguiDescSet_ = ImGui_ImplVulkan_AddTexture(
        sampler_, colorView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void ViewportTarget::resize(GpuDevice& gpu, uint32_t w, uint32_t h) {
    if (w == w_ && h == h_) return;
    gpu.syncDevice();
    destroy(device_);
    create(gpu, gamePass_, raytracePass_, colorFormat_, depthFormat_, w, h);
}

ViewportTarget::ViewportTarget(ViewportTarget&& o) noexcept
    : device_(o.device_), gamePass_(o.gamePass_), raytracePass_(o.raytracePass_),
      colorFormat_(o.colorFormat_), depthFormat_(o.depthFormat_),
      colorImage_(o.colorImage_), colorMem_(o.colorMem_), colorView_(o.colorView_),
      sampler_(o.sampler_),
      depthImage_(o.depthImage_), depthMem_(o.depthMem_), depthView_(o.depthView_),
      framebuffer_(o.framebuffer_), raytraceFramebuffer_(o.raytraceFramebuffer_),
      imguiDescSet_(o.imguiDescSet_),
      w_(o.w_), h_(o.h_) {
    o.device_               = VK_NULL_HANDLE;
    o.raytracePass_         = VK_NULL_HANDLE;
    o.colorImage_  = VK_NULL_HANDLE; o.colorMem_  = VK_NULL_HANDLE; o.colorView_  = VK_NULL_HANDLE;
    o.sampler_     = VK_NULL_HANDLE;
    o.depthImage_  = VK_NULL_HANDLE; o.depthMem_  = VK_NULL_HANDLE; o.depthView_  = VK_NULL_HANDLE;
    o.framebuffer_ = VK_NULL_HANDLE; o.raytraceFramebuffer_ = VK_NULL_HANDLE;
    o.imguiDescSet_ = VK_NULL_HANDLE;
}

ViewportTarget& ViewportTarget::operator=(ViewportTarget&& o) noexcept {
    if (this != &o) {
        if (device_ != VK_NULL_HANDLE) destroy(device_);
        new (this) ViewportTarget(std::move(o));
    }
    return *this;
}

void ViewportTarget::destroy(VkDevice device) {
    if (imguiDescSet_ != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(imguiDescSet_);
        imguiDescSet_ = VK_NULL_HANDLE;
    }
    if (raytraceFramebuffer_ != VK_NULL_HANDLE) { vkDestroyFramebuffer(device, raytraceFramebuffer_, nullptr); raytraceFramebuffer_ = VK_NULL_HANDLE; }
    if (framebuffer_ != VK_NULL_HANDLE) { vkDestroyFramebuffer(device, framebuffer_, nullptr); framebuffer_ = VK_NULL_HANDLE; }
    if (sampler_     != VK_NULL_HANDLE) { vkDestroySampler    (device, sampler_,     nullptr); sampler_     = VK_NULL_HANDLE; }
    if (colorView_   != VK_NULL_HANDLE) { vkDestroyImageView  (device, colorView_,   nullptr); colorView_   = VK_NULL_HANDLE; }
    if (colorImage_  != VK_NULL_HANDLE) { vkDestroyImage      (device, colorImage_,  nullptr); colorImage_  = VK_NULL_HANDLE; }
    if (colorMem_    != VK_NULL_HANDLE) { vkFreeMemory         (device, colorMem_,    nullptr); colorMem_    = VK_NULL_HANDLE; }
    if (depthView_   != VK_NULL_HANDLE) { vkDestroyImageView  (device, depthView_,   nullptr); depthView_   = VK_NULL_HANDLE; }
    if (depthImage_  != VK_NULL_HANDLE) { vkDestroyImage      (device, depthImage_,  nullptr); depthImage_  = VK_NULL_HANDLE; }
    if (depthMem_    != VK_NULL_HANDLE) { vkFreeMemory         (device, depthMem_,    nullptr); depthMem_    = VK_NULL_HANDLE; }
}
#endif
