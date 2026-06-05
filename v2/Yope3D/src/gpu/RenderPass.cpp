#include "RenderPass.h"
#include <stdexcept>

RenderPass::RenderPass(VkDevice device, VkFormat colorFormat, VkFormat depthFormat) : device(device) {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format         = colorFormat;
    colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format         = depthFormat;
    depthAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    // This EXTERNAL dependency serves two roles:
    //  1. Color: make attachment writes wait on the image-available semaphore.
    //  2. Depth: the depth buffer is a SINGLE persistent image reused every frame
    //     and is NOT covered by the present/acquire semaphores. Without a
    //     write-after-write barrier from the previous frame's depth writes (which
    //     complete at LATE_FRAGMENT_TESTS) to this frame's clear/layout transition,
    //     the two race — the SYNC-HAZARD-WRITE-AFTER-WRITE the validation layer
    //     reports (a genuine hazard, not a false positive). Hence LATE_FRAGMENT_TESTS
    //     + DEPTH_STENCIL_ATTACHMENT_WRITE on the source.
    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                      | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                      | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription attachments[2] = {colorAttachment, depthAttachment};
    VkRenderPassCreateInfo createInfo{};
    createInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = 2;
    createInfo.pAttachments    = attachments;
    createInfo.subpassCount    = 1;
    createInfo.pSubpasses      = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies   = &dep;

    if (vkCreateRenderPass(device, &createInfo, nullptr, &renderPass) != VK_SUCCESS)
        throw std::runtime_error("Failed to create render pass");
}

RenderPass RenderPass::createUIPass(VkDevice device, VkFormat colorFormat) {
    // Load the color attachment (preserve the 3D render underneath).
    // The 3D render pass leaves the image in PRESENT_SRC_KHR; we transition
    // to COLOR_ATTACHMENT_OPTIMAL for drawing, then back for presentation.
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format         = colorFormat;
    colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    // Ensure the 3D render pass's color writes are visible before the UI pass reads/writes.
    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo createInfo{};
    createInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = 1;
    createInfo.pAttachments    = &colorAttachment;
    createInfo.subpassCount    = 1;
    createInfo.pSubpasses      = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies   = &dep;

    VkRenderPass rp = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device, &createInfo, nullptr, &rp) != VK_SUCCESS)
        throw std::runtime_error("Failed to create UI render pass");
    return RenderPass(device, rp);
}

RenderPass RenderPass::createRaytracePass(VkDevice device, VkFormat colorFormat) {
    // Clears color at the start (raytracer writes every pixel), no depth attachment,
    // initialLayout = UNDEFINED, finalLayout = PRESENT_SRC_KHR.
    // The UI pass that follows expects PRESENT_SRC_KHR as initialLayout.
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format         = colorFormat;
    colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    // Wait for the compute shader to finish writing before the fragment stage reads.
    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo createInfo{};
    createInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = 1;
    createInfo.pAttachments    = &colorAttachment;
    createInfo.subpassCount    = 1;
    createInfo.pSubpasses      = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies   = &dep;

    VkRenderPass rp = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device, &createInfo, nullptr, &rp) != VK_SUCCESS)
        throw std::runtime_error("Failed to create raytrace render pass");
    return RenderPass(device, rp);
}

RenderPass RenderPass::createOffscreenUIPass(VkDevice device, VkFormat colorFormat) {
    // Mirrors createUIPass but targets the editor's offscreen viewport color
    // image instead of a swapchain image, so the layouts stay in
    // SHADER_READ_ONLY_OPTIMAL (left there by the offscreen game pass; left
    // there for ImGui sampling).
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format         = colorFormat;
    colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    // NOTE: this pass shares its pipeline (uiPipeline) with the swapchain UI pass,
    // which has one dependency — keep the dependency layout matching so the two
    // render passes stay pipeline-compatible. The UI-write → ImGui-sample ordering
    // is instead handled by an explicit image barrier after this pass (see
    // Renderer::beginFrameForEditor), which doesn't affect render-pass compatibility.
    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo createInfo{};
    createInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = 1;
    createInfo.pAttachments    = &colorAttachment;
    createInfo.subpassCount    = 1;
    createInfo.pSubpasses      = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies   = &dep;

    VkRenderPass rp = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device, &createInfo, nullptr, &rp) != VK_SUCCESS)
        throw std::runtime_error("Failed to create offscreen UI render pass");
    return RenderPass(device, rp);
}

RenderPass RenderPass::createOffscreenGamePass(VkDevice device, VkFormat colorFormat,
                                               VkFormat depthFormat) {
    // Identical to the main 3D render pass constructor, but finalLayout is
    // SHADER_READ_ONLY_OPTIMAL so ImGui can sample the texture without an explicit barrier.
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format         = colorFormat;
    colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format         = depthFormat;
    depthAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    // Same persistent-depth-buffer write-after-write barrier as the main pass
    // (see the constructor): LATE_FRAGMENT_TESTS + DEPTH_STENCIL_ATTACHMENT_WRITE
    // on the source so this frame's depth clear waits for the previous frame's
    // depth writes on the shared depth image.
    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                      | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                      | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription attachments[2] = {colorAttachment, depthAttachment};
    VkRenderPassCreateInfo createInfo{};
    createInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = 2;
    createInfo.pAttachments    = attachments;
    createInfo.subpassCount    = 1;
    createInfo.pSubpasses      = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies   = &dep;

    VkRenderPass rp = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device, &createInfo, nullptr, &rp) != VK_SUCCESS)
        throw std::runtime_error("Failed to create offscreen game render pass");
    return RenderPass(device, rp);
}

RenderPass RenderPass::createImGuiPass(VkDevice device, VkFormat colorFormat) {
    // ImGui owns the entire frame: clear swapchain, render all panels, present.
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format         = colorFormat;
    colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    // Ensure the offscreen game pass finishes before ImGui samples from it.
    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo createInfo{};
    createInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = 1;
    createInfo.pAttachments    = &colorAttachment;
    createInfo.subpassCount    = 1;
    createInfo.pSubpasses      = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies   = &dep;

    VkRenderPass rp = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device, &createInfo, nullptr, &rp) != VK_SUCCESS)
        throw std::runtime_error("Failed to create ImGui render pass");
    return RenderPass(device, rp);
}

RenderPass RenderPass::createOffscreenRaytracePass(VkDevice device, VkFormat colorFormat) {
    VkAttachmentDescription col{};
    col.format         = colorFormat;
    col.samples        = VK_SAMPLE_COUNT_1_BIT;
    col.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    col.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    col.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    col.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    col.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    col.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub{};
    sub.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments    = &ref;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments    = &col;
    ci.subpassCount    = 1;
    ci.pSubpasses      = &sub;
    ci.dependencyCount = 1;
    ci.pDependencies   = &dep;

    VkRenderPass rp = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device, &ci, nullptr, &rp) != VK_SUCCESS)
        throw std::runtime_error("Failed to create offscreen raytrace render pass");
    return RenderPass(device, rp);
}

RenderPass::~RenderPass() {
    if (renderPass != VK_NULL_HANDLE)
        vkDestroyRenderPass(device, renderPass, nullptr);
}
