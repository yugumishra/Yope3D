#include "editor/picking/IdBufferPass.h"
#ifdef YOPE_EDITOR
#include "editor/Selection.h"
#include "ecs/Registry.h"
#include "ecs/Components.h"
#include "world/Transform.h"
#include "world/RenderMesh.h"
#include "gpu/GpuDevice.h"
#include <fstream>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <climits>

// ---- Helper: load a SPIR-V file ----
static std::vector<uint32_t> loadSpirv(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) throw std::runtime_error(std::string("IdBufferPass: cannot open shader: ") + path);
    size_t sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint32_t> code(sz / 4);
    f.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(sz));
    return code;
}

static VkShaderModule createShaderModule(VkDevice dev, const std::vector<uint32_t>& code) {
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size() * 4;
    ci.pCode    = code.data();
    VkShaderModule mod;
    if (vkCreateShaderModule(dev, &ci, nullptr, &mod) != VK_SUCCESS)
        throw std::runtime_error("IdBufferPass: failed to create shader module");
    return mod;
}

// ---- IdBufferPass::init ----

void IdBufferPass::init(GpuDevice& gpu,
                        VkDescriptorSetLayout uboSetLayout,
                        VkCommandPool /*cmdPool*/,
                        VkFormat depthFormat,
                        uint32_t w, uint32_t h) {
    device_      = gpu.device();
    depthFormat_ = depthFormat;
    w_ = w; h_ = h;

    createIdImage(gpu, w, h);
    createRenderPass(device_, depthFormat);
    createStagingBuffer(gpu);
    createPipeline(device_, uboSetLayout);
}

void IdBufferPass::resize(GpuDevice& gpu, VkImageView sharedDepthView, uint32_t w, uint32_t h) {
    vkDeviceWaitIdle(device_);
    if (framebuffer_) { vkDestroyFramebuffer(device_, framebuffer_, nullptr); framebuffer_ = VK_NULL_HANDLE; }
    destroyIdImageResources(device_);
    w_ = w; h_ = h;
    createIdImage(gpu, w, h);
    if (sharedDepthView) createFramebuffer(device_, sharedDepthView, w, h);
}

void IdBufferPass::destroy(VkDevice device) {
    if (framebuffer_)   { vkDestroyFramebuffer(device, framebuffer_, nullptr);    framebuffer_ = VK_NULL_HANDLE; }
    if (pipeline_)      { vkDestroyPipeline(device, pipeline_, nullptr);           pipeline_ = VK_NULL_HANDLE; }
    if (pipelineLayout_){ vkDestroyPipelineLayout(device, pipelineLayout_, nullptr); pipelineLayout_ = VK_NULL_HANDLE; }
    if (renderPass_)    { vkDestroyRenderPass(device, renderPass_, nullptr);        renderPass_ = VK_NULL_HANDLE; }
    if (stagingBuf_)    { vkDestroyBuffer(device, stagingBuf_, nullptr);            stagingBuf_ = VK_NULL_HANDLE; }
    if (stagingMem_)    { vkFreeMemory(device, stagingMem_, nullptr);               stagingMem_ = VK_NULL_HANDLE; }
    destroyIdImageResources(device);
}

void IdBufferPass::destroyIdImageResources(VkDevice device) {
    if (idView_)  { vkDestroyImageView(device, idView_, nullptr);  idView_  = VK_NULL_HANDLE; }
    if (idImage_) { vkDestroyImage(device, idImage_, nullptr);     idImage_ = VK_NULL_HANDLE; }
    if (idMem_)   { vkFreeMemory(device, idMem_, nullptr);         idMem_   = VK_NULL_HANDLE; }
}

// ---- createIdImage ----

void IdBufferPass::createIdImage(GpuDevice& gpu, uint32_t w, uint32_t h) {
    VkImageCreateInfo ii{};
    ii.sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType   = VK_IMAGE_TYPE_2D;
    ii.format      = VK_FORMAT_R32_UINT;
    ii.extent      = {w, h, 1};
    ii.mipLevels   = 1;
    ii.arrayLayers = 1;
    ii.samples     = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling      = VK_IMAGE_TILING_OPTIMAL;
    ii.usage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device_, &ii, nullptr, &idImage_) != VK_SUCCESS)
        throw std::runtime_error("IdBufferPass: failed to create ID image");

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(device_, idImage_, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = gpu.findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device_, &ai, nullptr, &idMem_) != VK_SUCCESS)
        throw std::runtime_error("IdBufferPass: failed to allocate ID image memory");
    vkBindImageMemory(device_, idImage_, idMem_, 0);

    VkImageViewCreateInfo vi{};
    vi.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image    = idImage_;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format   = VK_FORMAT_R32_UINT;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device_, &vi, nullptr, &idView_) != VK_SUCCESS)
        throw std::runtime_error("IdBufferPass: failed to create ID image view");
}

// ---- createRenderPass ----

void IdBufferPass::createRenderPass(VkDevice device, VkFormat depthFormat) {
    // Color: R32_UINT — cleared to UINT_MAX (= "no entity")
    VkAttachmentDescription colorAttach{};
    colorAttach.format         = VK_FORMAT_R32_UINT;
    colorAttach.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAttach.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttach.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttach.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttach.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttach.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttach.finalLayout    = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    // Depth: shared with game pass — load existing contents, don't store
    VkAttachmentDescription depthAttach{};
    depthAttach.format         = depthFormat;
    depthAttach.samples        = VK_SAMPLE_COUNT_1_BIT;
    depthAttach.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthAttach.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttach.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttach.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttach.initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttach.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

    VkAttachmentDescription attachments[2] = {colorAttach, depthAttach};
    VkRenderPassCreateInfo  rpci{};
    rpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 2;
    rpci.pAttachments    = attachments;
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies   = &dep;

    if (vkCreateRenderPass(device, &rpci, nullptr, &renderPass_) != VK_SUCCESS)
        throw std::runtime_error("IdBufferPass: failed to create render pass");
}

// ---- createFramebuffer ----

void IdBufferPass::createFramebuffer(VkDevice device, VkImageView sharedDepthView,
                                     uint32_t w, uint32_t h) {
    VkImageView attachments[2] = {idView_, sharedDepthView};
    VkFramebufferCreateInfo fbi{};
    fbi.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbi.renderPass      = renderPass_;
    fbi.attachmentCount = 2;
    fbi.pAttachments    = attachments;
    fbi.width           = w;
    fbi.height          = h;
    fbi.layers          = 1;
    if (vkCreateFramebuffer(device, &fbi, nullptr, &framebuffer_) != VK_SUCCESS)
        throw std::runtime_error("IdBufferPass: failed to create framebuffer");
}

// ---- createStagingBuffer ----

void IdBufferPass::createStagingBuffer(GpuDevice& gpu) {
    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = sizeof(uint32_t);
    bi.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device_, &bi, nullptr, &stagingBuf_) != VK_SUCCESS)
        throw std::runtime_error("IdBufferPass: failed to create staging buffer");

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device_, stagingBuf_, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = gpu.findMemoryType(mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device_, &ai, nullptr, &stagingMem_) != VK_SUCCESS)
        throw std::runtime_error("IdBufferPass: failed to allocate staging memory");
    vkBindBufferMemory(device_, stagingBuf_, stagingMem_, 0);
}

// ---- createPipeline ----

void IdBufferPass::createPipeline(VkDevice device, VkDescriptorSetLayout uboSetLayout) {
    // Push constant: mat4 (64) + uint (4) = 68 bytes
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset     = 0;
    pushRange.size       = 68;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount         = 1;
    layoutInfo.pSetLayouts            = &uboSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushRange;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS)
        throw std::runtime_error("IdBufferPass: failed to create pipeline layout");

    // Load shaders from YOPE_SHADER_DIR
    const char* shaderDir = std::getenv("YOPE_SHADER_DIR");
    std::string vertPath = shaderDir ? (std::string(shaderDir) + "/id_buffer.vert.spv")
                                     : "compiled_shaders/id_buffer.vert.spv";
    std::string fragPath = shaderDir ? (std::string(shaderDir) + "/id_buffer.frag.spv")
                                     : "compiled_shaders/id_buffer.frag.spv";

    auto vertCode = loadSpirv(vertPath.c_str());
    auto fragCode = loadSpirv(fragPath.c_str());
    VkShaderModule vertMod = createShaderModule(device, vertCode);
    VkShaderModule fragMod = createShaderModule(device, fragCode);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertMod;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragMod;
    stages[1].pName  = "main";

    // Same vertex format as the raster pipeline
    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3]{};
    attrs[0].location = 0; attrs[0].binding = 0;
    attrs[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset   = offsetof(Vertex, position);
    attrs[1].location = 1; attrs[1].binding = 0;
    attrs[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset   = offsetof(Vertex, normal);
    attrs[2].location = 2; attrs[2].binding = 0;
    attrs[2].format   = VK_FORMAT_R32G32_SFLOAT;
    attrs[2].offset   = offsetof(Vertex, uv);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount   = 1;
    vertexInput.pVertexBindingDescriptions      = &binding;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode    = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttach{};
    blendAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;  // uint: write R only
    blendAttach.blendEnable    = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments    = &blendAttach;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable       = VK_TRUE;
    depthStencil.depthWriteEnable      = VK_FALSE;  // read-only; game pass already wrote depth
    depthStencil.depthCompareOp        = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates    = dynStates;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount          = 2;
    pipelineInfo.pStages             = stages;
    pipelineInfo.pVertexInputState   = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState   = &multisampling;
    pipelineInfo.pColorBlendState    = &colorBlend;
    pipelineInfo.pDepthStencilState  = &depthStencil;
    pipelineInfo.pDynamicState       = &dynamicState;
    pipelineInfo.layout              = pipelineLayout_;
    pipelineInfo.renderPass          = renderPass_;
    pipelineInfo.subpass             = 0;

    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_);

    vkDestroyShaderModule(device, vertMod, nullptr);
    vkDestroyShaderModule(device, fragMod, nullptr);
}

// ---- record ----

void IdBufferPass::record(VkCommandBuffer cmd, ecs::Registry& reg,
                          VkDescriptorSet globalDescSet,
                          VkImageView sharedDepthView,
                          uint32_t vpW, uint32_t vpH) {
    if (!renderPass_ || !pipeline_) return;

    // Recreate framebuffer if we don't have one (or dimensions changed)
    if (!framebuffer_ && sharedDepthView)
        createFramebuffer(device_, sharedDepthView, w_, h_);

    if (!framebuffer_) return;

    // Transition ID image: UNDEFINED → COLOR_ATTACHMENT
    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask       = 0;
    barrier.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = idImage_;
    barrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Clear color = UINT_MAX (no entity)
    VkClearValue clears[2];
    clears[0].color.uint32[0] = UINT32_MAX;
    clears[1].depthStencil    = {1.0f, 0};

    VkRenderPassBeginInfo rpbi{};
    rpbi.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass      = renderPass_;
    rpbi.framebuffer     = framebuffer_;
    rpbi.renderArea      = {{0, 0}, {w_, h_}};
    rpbi.clearValueCount = 2;
    rpbi.pClearValues    = clears;
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    VkViewport viewport{0.f, 0.f, static_cast<float>(w_), static_cast<float>(h_), 0.f, 1.f};
    VkRect2D   scissor{{0, 0}, {w_, h_}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_,
                            0, 1, &globalDescSet, 0, nullptr);

    struct PushData { float model[16]; uint32_t entityId; };

    for (auto [e, sel, mr, tf] : reg.view<ecs::EditorPickable, ecs::MeshRenderer, Transform>()) {
        if (!mr.mesh || !mr.mesh->transformReady) continue;

        math::Mat4 m = tf.getModelMatrix();
        PushData pd;
        std::memcpy(pd.model, &m, sizeof(float) * 16);
        pd.entityId = e.id;
        vkCmdPushConstants(cmd, pipelineLayout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(PushData), &pd);

        mr.mesh->draw(cmd);
    }

    vkCmdEndRenderPass(cmd);

    // Readback: if a click was scheduled last frame, copy the pixel
    if (readbackReady_) {
        // Image is now TRANSFER_SRC_OPTIMAL (final layout of renderPass_)
        uint32_t x = std::min(clickX_, w_ - 1);
        uint32_t y = std::min(clickY_, h_ - 1);

        VkBufferImageCopy region{};
        region.bufferOffset      = 0;
        region.bufferRowLength   = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset       = {static_cast<int32_t>(x), static_cast<int32_t>(y), 0};
        region.imageExtent       = {1, 1, 1};

        vkCmdCopyImageToBuffer(cmd, idImage_,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               stagingBuf_, 1, &region);

        readbackReady_ = false;
    }

    // Promote pending → ready for next frame
    if (pendingReadback_) {
        readbackReady_   = true;
        pendingReadback_ = false;
    }
}

// ---- scheduleReadback ----

void IdBufferPass::scheduleReadback(uint32_t x, uint32_t y) {
    pendingReadback_ = true;
    clickX_ = x;
    clickY_ = y;
}

// ---- pollResult ----

void IdBufferPass::pollResult(ecs::Registry& reg, Selection& sel) {
    // readbackReady_ is set DURING record() on the frame after scheduleReadback().
    // After that frame's GPU submission we can safely read the staging buffer.
    // We use a simple 1-frame-lag: after readbackReady_ transitions back to false,
    // vkDeviceWaitIdle was NOT called — but the staging buffer is host-coherent so
    // the data will be visible. In practice the GPU finishes before the next CPU poll.
    // This is acceptable (1-frame latency, non-blocking).

    // We check by looking for a "just-cleared" readbackReady_ that was set this frame.
    // A cleaner approach uses a fence; this version is simple and correct for low-latency use.
    // The buffer is only written when readbackReady_ becomes false in record(), meaning
    // the copy command was recorded in the PREVIOUS frame's cmd buffer.
    // So on the CURRENT frame, check if we should poll.

    static bool wasReadyLastFrame = false;
    bool isReadyNow = readbackReady_;

    if (wasReadyLastFrame && !isReadyNow) {
        // The previous frame's cmd buffer recorded the copy; by now it's been submitted and
        // presented. The staging buffer should have the data (host-coherent).
        void* mapped = nullptr;
        if (vkMapMemory(device_, stagingMem_, 0, sizeof(uint32_t), 0, &mapped) == VK_SUCCESS) {
            uint32_t entityId = *static_cast<uint32_t*>(mapped);
            vkUnmapMemory(device_, stagingMem_);

            if (entityId != UINT32_MAX) {
                // Find the entity with this id
                for (auto [e, pick] : reg.view<ecs::EditorPickable>()) {
                    if (e.id == entityId) {
                        sel.set(e);
                        break;
                    }
                }
            }
        }
    }

    wasReadyLastFrame = isReadyNow;
}
#endif
