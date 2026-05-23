#include "Renderer.h"
#include "Camera.h"
#include "gpu/GpuDevice.h"
#include "gpu/Swapchain.h"
#include "gpu/RenderPass.h"
#include "gpu/ShaderModule.h"
#include "gpu/DescriptorSetLayout.h"
#include "gpu/DescriptorPool.h"
#include "gpu/Texture.h"
#include "math/Mat4.h"
#include "platform/Window.h"
#include "assets/AssetManager.h"
#include "ui/UIManager.h"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <array>
#include <vector>
#include <cstring>

// ---------------------------------------------------------------------------
// GlobalUBO — must match the std140 layout in triangle.vert exactly.
// view + proj: column-major Mat4 (16 floats each).
// cameraPos:   vec3 padded to 16 bytes per std140.
// numLights:   int (replaces _pad, same 4-byte slot).
// ---------------------------------------------------------------------------

struct GlobalUBO {
    math::Mat4 view;        // 64 bytes
    math::Mat4 proj;        // 64 bytes
    float      cameraPos[3]; // 12 bytes — vec3 body
    int        numLights;    //  4 bytes — std140 pads vec3 to vec4, now holds light count
};
static_assert(sizeof(GlobalUBO) == 144, "GlobalUBO size must match std140 layout");

// ---------------------------------------------------------------------------
// Default quad (normals pointing toward camera at +Z)
// ---------------------------------------------------------------------------

static const std::vector<Vertex> kDefaultVertices = {
    {{-0.5f, -0.5f, 0.5f}, { 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
    {{ 0.5f,  0.5f, 0.5f}, { 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
    {{-0.5f,  0.5f, 0.5f}, { 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
    {{ 0.5f, -0.5f, 0.5f}, { 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},

    {{-0.5f, -0.5f,-0.5f}, { 0.0f, 0.0f,-1.0f}, {0.0f, 0.0f}},
    {{ 0.5f,  0.5f,-0.5f}, { 0.0f, 0.0f,-1.0f}, {1.0f, 1.0f}},
    {{-0.5f,  0.5f,-0.5f}, { 0.0f, 0.0f,-1.0f}, {0.0f, 1.0f}},
    {{ 0.5f, -0.5f,-0.5f}, { 0.0f, 0.0f,-1.0f}, {1.0f, 0.0f}},

    {{-0.5f,  0.5f,-0.5f}, { 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
    {{ 0.5f,  0.5f, 0.5f}, { 0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
    {{-0.5f,  0.5f, 0.5f}, { 0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
    {{ 0.5f,  0.5f,-0.5f}, { 0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},

    {{-0.5f, -0.5f,-0.5f}, { 0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
    {{ 0.5f, -0.5f, 0.5f}, { 0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
    {{-0.5f, -0.5f, 0.5f}, { 0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},
    {{ 0.5f, -0.5f,-0.5f}, { 0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},

    {{ 0.5f, -0.5f,-0.5f}, { 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
    {{ 0.5f,  0.5f, 0.5f}, { 1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
    {{ 0.5f, -0.5f, 0.5f}, { 1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
    {{ 0.5f,  0.5f,-0.5f}, { 1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},

    {{-0.5f, -0.5f,-0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
    {{-0.5f,  0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
    {{-0.5f, -0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
    {{-0.5f,  0.5f,-0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
};
static const std::vector<uint32_t> kDefaultIndices = { 
    0, 1, 2, 0, 3, 1,
    4, 5, 6, 4, 7, 5,
    8, 9, 10, 8, 11, 9,
    12, 13, 14, 12, 15, 13,
    16, 17, 18, 16, 19, 17,
    20, 21, 22, 20, 23, 21,
};

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Renderer::Renderer(GpuDevice& gpu, Window& window) {
    swapchain = std::make_unique<Swapchain>(gpu, window);
    depthBuffer = std::make_unique<DepthBuffer>(gpu, swapchain->extent().width, swapchain->extent().height);
    createRenderPass(gpu);
    createUBOLayout(gpu.device());
    createTextureSetLayout(gpu.device());
    createUniformBuffers(gpu);
    createLightBuffers(gpu);
    createDescriptorPool(gpu.device());
    createDescriptorSets(gpu.device());
    createPipeline(gpu.device());
    createFramebuffers(gpu.device());
    createUIRenderPass(gpu);
    createUIFramebuffers(gpu.device());
    createUIPipeline(gpu.device());
    createUIBuffers(gpu);
    createCommandPool(gpu);
    allocateCommandBuffers(gpu.device());
    createSyncObjects(gpu.device());
}

Renderer::~Renderer() {
    // Caller must have called waitIdle() before destroying.
}

VkDescriptorSetLayout Renderer::getTextureSetLayout() const {
    return textureSetLayout->get();
}

void Renderer::waitIdle(GpuDevice& gpu) {
    vkDeviceWaitIdle(gpu.device());

    for (int i = 0; i < MAX_FRAMES; ++i) {
        vkDestroySemaphore(gpu.device(), imageAvailable[i], nullptr);
        vkDestroyFence(gpu.device(), inFlightFence[i], nullptr);
    }
    for (auto& sem : renderFinished) vkDestroySemaphore(gpu.device(), sem, nullptr);
    renderFinished.clear();

    vkDestroyCommandPool(gpu.device(), commandPool, nullptr);
    destroyFramebuffers(gpu.device());

    vkDestroyPipeline(gpu.device(), pipeline, nullptr);
    vkDestroyPipelineLayout(gpu.device(), pipelineLayout, nullptr);

    // UI pipeline cleanup
    vkDestroyPipeline(gpu.device(), uiPipeline, nullptr);
    vkDestroyPipelineLayout(gpu.device(), uiPipelineLayout, nullptr);
    destroyUIFramebuffers(gpu.device());
    for (auto& ub : uiBuffers) ub.destroy(gpu.device());
    uiRenderPass.reset();

    for (auto& ub : uniformBuffers) ub.destroy(gpu.device());
    for (auto& sb : lightBuffers) sb.destroy(gpu.device());
    descriptorPool.reset();  // all descriptor sets freed implicitly
    uboLayout.reset();

    depthBuffer.reset();
    renderPass.reset();
    swapchain.reset();
}

// ---------------------------------------------------------------------------
// Render pass
// ---------------------------------------------------------------------------

void Renderer::createRenderPass(GpuDevice& gpu) {
    renderPass = std::make_unique<RenderPass>(gpu.device(), swapchain->imageFormat(), depthBuffer->format());
}

// ---------------------------------------------------------------------------
// Descriptor layout + pool + sets
// ---------------------------------------------------------------------------

void Renderer::createUBOLayout(VkDevice device) {
    // Binding 0: GlobalUBO (view, proj, cameraPos, numLights)
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding         = 0;
    uboBinding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 1: LightBuffer SSBO (read-only storage buffer for lights)
    VkDescriptorSetLayoutBinding ssboBinding{};
    ssboBinding.binding         = 1;
    ssboBinding.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ssboBinding.descriptorCount = 1;
    ssboBinding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;  // Fragment stage only

    uboLayout = std::make_unique<DescriptorSetLayout>(device, std::vector{uboBinding, ssboBinding});
}

void Renderer::createTextureSetLayout(VkDevice device) {
    // Set 1 layout: binding 0 = combined image sampler (per-texture descriptor set)
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding         = 0;
    samplerBinding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    textureSetLayout = std::make_unique<DescriptorSetLayout>(device, std::vector{samplerBinding});
}

void Renderer::createUniformBuffers(GpuDevice& gpu) {
    for (auto& ub : uniformBuffers)
        ub = UniformBuffer(gpu, sizeof(GlobalUBO));
}

void Renderer::createLightBuffers(GpuDevice& gpu) {
    // Allocate worst-case size: YOPE_MAX_LIGHTS * LIGHT_MAX_FLOATS * sizeof(float)
    // (SpotLight needs 15 floats, which is the max)
    VkDeviceSize lightBufferSize = YOPE_MAX_LIGHTS * LIGHT_MAX_FLOATS * sizeof(float);
    for (auto& sb : lightBuffers)
        sb = StorageBuffer(gpu, lightBufferSize);
}

void Renderer::createDescriptorPool(VkDevice device) {
    // Pool sizes for both UBO (binding 0) and SSBO (binding 1) per frame
    VkDescriptorPoolSize poolSizes[2]{};

    poolSizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = MAX_FRAMES;  // one UBO per frame

    poolSizes[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = MAX_FRAMES;  // one SSBO per frame

    descriptorPool = std::make_unique<DescriptorPool>(device, std::vector{poolSizes[0], poolSizes[1]}, MAX_FRAMES);
}

void Renderer::createDescriptorSets(VkDevice device) {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES, uboLayout->get());
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = descriptorPool->get();
    ai.descriptorSetCount = MAX_FRAMES;
    ai.pSetLayouts        = layouts.data();
    if (vkAllocateDescriptorSets(device, &ai, descriptorSets.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate descriptor sets");

    for (int i = 0; i < MAX_FRAMES; ++i) {
        // Write binding 0: GlobalUBO
        VkDescriptorBufferInfo uboBuf{};
        uboBuf.buffer = uniformBuffers[i].get();
        uboBuf.offset = 0;
        uboBuf.range  = sizeof(GlobalUBO);

        VkWriteDescriptorSet uboWrite{};
        uboWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uboWrite.dstSet          = descriptorSets[i];
        uboWrite.dstBinding      = 0;
        uboWrite.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboWrite.descriptorCount = 1;
        uboWrite.pBufferInfo     = &uboBuf;

        // Write binding 1: LightBuffer SSBO
        VkDescriptorBufferInfo ssboBuf{};
        ssboBuf.buffer = lightBuffers[i].get();
        ssboBuf.offset = 0;
        ssboBuf.range  = lightBuffers[i].getSize();

        VkWriteDescriptorSet ssboWrite{};
        ssboWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ssboWrite.dstSet          = descriptorSets[i];
        ssboWrite.dstBinding      = 1;
        ssboWrite.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ssboWrite.descriptorCount = 1;
        ssboWrite.pBufferInfo     = &ssboBuf;

        VkWriteDescriptorSet writes[2] = {uboWrite, ssboWrite};
        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
    }
}

// ---------------------------------------------------------------------------
// Graphics pipeline
// ---------------------------------------------------------------------------

void Renderer::createPipeline(VkDevice device) {
    ShaderModule vert(device, std::string(YOPE_SHADER_DIR) + "/triangle.vert.spv");
    ShaderModule frag(device, std::string(YOPE_SHADER_DIR) + "/triangle.frag.spv");

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert.get();
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag.get();
    stages[1].pName  = "main";

    // Vertex input — one binding, three attributes (position, normal, uv)
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

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates    = dynStates;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode    = VK_CULL_MODE_NONE;
    rasterizer.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments    = &blendAttachment;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable       = VK_TRUE;
    depthStencil.depthWriteEnable      = VK_TRUE;
    depthStencil.depthCompareOp        = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable     = VK_FALSE;

    // Per-object model matrix + color + state via push constants.
    // Layout: mat4 model (64 bytes) + vec3 color (12 bytes) + int state (4 bytes) = 80 bytes.
    // Accessible to both vertex and fragment stages.
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset     = 0;
    pushRange.size       = 80;  // mat4 + vec3 + int

    VkDescriptorSetLayout setLayouts[2] = {uboLayout->get(), textureSetLayout->get()};
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount         = 2;
    layoutInfo.pSetLayouts            = setLayouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushRange;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create pipeline layout");

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
    pipelineInfo.layout              = pipelineLayout;
    pipelineInfo.renderPass          = renderPass->get();
    pipelineInfo.subpass             = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create graphics pipeline");
}

// ---------------------------------------------------------------------------
// Framebuffers
// ---------------------------------------------------------------------------

void Renderer::createFramebuffers(VkDevice device) {
    const auto& views = swapchain->imageViews();
    framebuffers.resize(views.size());
    for (size_t i = 0; i < views.size(); ++i) {
        VkImageView attachments[2] = {views[i], depthBuffer->imageView()};

        VkFramebufferCreateInfo fi{};
        fi.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass      = renderPass->get();
        fi.attachmentCount = 2;
        fi.pAttachments    = attachments;
        fi.width           = swapchain->extent().width;
        fi.height          = swapchain->extent().height;
        fi.layers          = 1;
        if (vkCreateFramebuffer(device, &fi, nullptr, &framebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create framebuffer");
    }
}

void Renderer::destroyFramebuffers(VkDevice device) {
    for (auto fb : framebuffers) vkDestroyFramebuffer(device, fb, nullptr);
    framebuffers.clear();
}

// ---------------------------------------------------------------------------
// Command pool + buffers
// ---------------------------------------------------------------------------

void Renderer::createCommandPool(GpuDevice& gpu) {
    VkCommandPoolCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = gpu.queueFamilies().graphics.value();
    if (vkCreateCommandPool(gpu.device(), &ci, nullptr, &commandPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create command pool");
}

void Renderer::allocateCommandBuffers(VkDevice device) {
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = commandPool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = MAX_FRAMES;
    if (vkAllocateCommandBuffers(device, &ai, cmdBuffers.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate command buffers");
}

// ---------------------------------------------------------------------------
// Sync objects
// ---------------------------------------------------------------------------

void Renderer::createSyncObjects(VkDevice device) {
    VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo     fi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES; ++i) {
        if (vkCreateSemaphore(device, &si, nullptr, &imageAvailable[i]) != VK_SUCCESS ||
            vkCreateFence(device, &fi, nullptr, &inFlightFence[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create sync objects");
    }

    // renderFinished is per-swapchain-image (not per-frame) to avoid MAILBOX reuse races.
    renderFinished.resize(swapchain->imageCount());
    for (auto& sem : renderFinished)
        if (vkCreateSemaphore(device, &si, nullptr, &sem) != VK_SUCCESS)
            throw std::runtime_error("Failed to create renderFinished semaphore");
}

// ---------------------------------------------------------------------------
// Swapchain recreation
// ---------------------------------------------------------------------------

void Renderer::recreateSwapchain(GpuDevice& gpu, Window& window) {
    int w = 0, h = 0;
    while (w == 0 || h == 0) {
        glfwGetFramebufferSize(window.getHandle(), &w, &h);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(gpu.device());

    for (auto& sem : renderFinished) vkDestroySemaphore(gpu.device(), sem, nullptr);
    renderFinished.clear();

    destroyFramebuffers(gpu.device());
    destroyUIFramebuffers(gpu.device());
    depthBuffer.reset();
    swapchain->recreate(gpu, window);
    depthBuffer = std::make_unique<DepthBuffer>(gpu, swapchain->extent().width, swapchain->extent().height);
    createFramebuffers(gpu.device());
    createUIFramebuffers(gpu.device());

    VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    renderFinished.resize(swapchain->imageCount());
    for (auto& sem : renderFinished)
        if (vkCreateSemaphore(gpu.device(), &si, nullptr, &sem) != VK_SUCCESS)
            throw std::runtime_error("Failed to recreate renderFinished semaphores");
}

// ---------------------------------------------------------------------------
// Command buffer recording
// ---------------------------------------------------------------------------

void Renderer::recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex, const World& world,
                                  AssetManager& assets) {
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &bi);

    VkClearValue clearValues[2]{};
    clearValues[0].color = {0.05f, 0.05f, 0.05f, 1.0f};
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpbi{};
    rpbi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass        = renderPass->get();
    rpbi.framebuffer       = framebuffers[imageIndex];
    rpbi.renderArea.offset = {0, 0};
    rpbi.renderArea.extent = swapchain->extent();
    rpbi.clearValueCount   = 2;
    rpbi.pClearValues      = clearValues;

    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(swapchain->extent().width);
    viewport.height   = static_cast<float>(swapchain->extent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{ {0, 0}, swapchain->extent() };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind the per-frame global UBO + SSBO (set 0: view + proj + cameraPos + lights).
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
        0, 1, &descriptorSets[currentFrame], 0, nullptr);

    // In debug mode, skip normal meshes — debug shapes are drawn exclusively below.
    if (!world.debugPhysics)
    for (auto* mesh : world.getRenderMeshes()) {
        if (!mesh->transformReady) continue;
        // Bind the mesh's texture descriptor set (set 1).
        // If the mesh has no texture, use the default white texture.
        Texture* textureToUse = mesh->texture ? mesh->texture : assets.getDefaultTexture();
        VkDescriptorSet textureSet = textureToUse->getDescriptorSet();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
            1, 1, &textureSet, 0, nullptr);

        // Prepare push constants: identity model matrix + per-mesh color + state.
        struct PushConstants {
            math::Mat4 model;
            float      color[3];
            int32_t    state;
        } push{};
        push.model = mesh->modelMatrix;
        push.color[0] = mesh->color[0];
        push.color[1] = mesh->color[1];
        push.color[2] = mesh->color[2];
        push.state = mesh->state;

        vkCmdPushConstants(cmd, pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, 80, &push);

        mesh->draw(cmd);
    }

    // Physics debug overlay — solid-colored shapes at each hull's actual collision extent.
    if (world.debugPhysics) {
        // Pipeline always expects set 1 (texture). Bind the default white texture once
        // for all debug draws — the shader uses STATE_SOLID so it ignores it.
        VkDescriptorSet defaultTex = assets.getDefaultTexture()->getDescriptorSet();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
            1, 1, &defaultTex, 0, nullptr);

        for (const auto& dm : world.getDebugMeshes()) {
            if (!dm) continue; // intangible hull placeholder (e.g. player sphere)
            struct PushConstants { math::Mat4 model; float color[3]; int32_t state; } push{};
            push.model    = dm->modelMatrix;
            push.color[0] = 0.0f; push.color[1] = 1.0f; push.color[2] = 0.2f; // bright green
            push.state    = 0; // STATE_SOLID
            vkCmdPushConstants(cmd, pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, 80, &push);
            dm->draw(cmd);
        }
    }

    vkCmdEndRenderPass(cmd);

    // ---------------------------------------------------------------------------
    // UI render pass — runs after the 3D pass, loads color, no depth.
    // ---------------------------------------------------------------------------
    if (uiManager_) {
        const auto& drawCalls = uiManager_->getDrawCalls();
        if (!drawCalls.empty()) {
            VkRenderPassBeginInfo uiRpbi{};
            uiRpbi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            uiRpbi.renderPass        = uiRenderPass->get();
            uiRpbi.framebuffer       = uiFramebuffers[imageIndex];
            uiRpbi.renderArea.offset = {0, 0};
            uiRpbi.renderArea.extent = swapchain->extent();
            uiRpbi.clearValueCount   = 0;   // LOAD — no clear

            vkCmdBeginRenderPass(cmd, &uiRpbi, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, uiPipeline);

            VkViewport viewport{};
            viewport.width    = static_cast<float>(swapchain->extent().width);
            viewport.height   = static_cast<float>(swapchain->extent().height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);

            VkRect2D scissor{ {0, 0}, swapchain->extent() };
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            const UIBuffer& ubuf = uiBuffers[currentFrame];
            VkBuffer vb = ubuf.vertexBuffer();
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
            vkCmdBindIndexBuffer(cmd, ubuf.indexBuffer(), 0, VK_INDEX_TYPE_UINT32);

            for (const auto& dc : drawCalls) {
                if (dc.texture != VK_NULL_HANDLE) {
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        uiPipelineLayout, 0, 1, &dc.texture, 0, nullptr);
                }
                vkCmdPushConstants(cmd, uiPipelineLayout,
                    VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(int32_t), &dc.state);
                vkCmdDrawIndexed(cmd, dc.indexCount, 1, dc.indexOffset,
                                 dc.vertexOffset, 0);
            }

            vkCmdEndRenderPass(cmd);
        }
    }

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
        throw std::runtime_error("Failed to record command buffer");
}

// ---------------------------------------------------------------------------
// destroyFramebuffers helpers
// ---------------------------------------------------------------------------

void Renderer::destroyUIFramebuffers(VkDevice device) {
    for (auto fb : uiFramebuffers) vkDestroyFramebuffer(device, fb, nullptr);
    uiFramebuffers.clear();
}

// ---------------------------------------------------------------------------
// UI pipeline setup
// ---------------------------------------------------------------------------

void Renderer::createUIRenderPass(GpuDevice& gpu) {
    uiRenderPass = std::make_unique<RenderPass>(
        RenderPass::createUIPass(gpu.device(), swapchain->imageFormat()));
}

void Renderer::createUIFramebuffers(VkDevice device) {
    const auto& views = swapchain->imageViews();
    uiFramebuffers.resize(views.size());
    for (size_t i = 0; i < views.size(); ++i) {
        VkFramebufferCreateInfo fi{};
        fi.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass      = uiRenderPass->get();
        fi.attachmentCount = 1;
        fi.pAttachments    = &views[i];   // color only — no depth for UI
        fi.width           = swapchain->extent().width;
        fi.height          = swapchain->extent().height;
        fi.layers          = 1;
        if (vkCreateFramebuffer(device, &fi, nullptr, &uiFramebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create UI framebuffer");
    }
}

void Renderer::createUIPipeline(VkDevice device) {
    ShaderModule vert(device, std::string(YOPE_SHADER_DIR) + "/ui.vert.spv");
    ShaderModule frag(device, std::string(YOPE_SHADER_DIR) + "/ui.frag.spv");

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert.get();
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag.get();
    stages[1].pName  = "main";

    // UIVertex layout: vec2 pos @ 0, vec2 uv @ 1, vec4 color @ 2
    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(UIVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3]{};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(UIVertex, x) };
    attrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(UIVertex, u) };
    attrs[2] = { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UIVertex, r) };

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount   = 1;
    vertexInput.pVertexBindingDescriptions      = &binding;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates    = dynStates;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode    = VK_CULL_MODE_NONE;
    rasterizer.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth: disabled for UI (renders on top of 3D scene)
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable  = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.stencilTestEnable= VK_FALSE;

    // Alpha blending: src_alpha / one_minus_src_alpha
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable         = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;
    blendAttachment.colorWriteMask      =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments    = &blendAttachment;

    // Push constant: just int state (4 bytes), fragment stage only.
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset     = 0;
    pushRange.size       = 4;

    // Set 0 = texture sampler (reuse the same layout as the 3D pipeline's set 1)
    VkDescriptorSetLayout setLayouts[1] = { textureSetLayout->get() };
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount         = 1;
    layoutInfo.pSetLayouts            = setLayouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushRange;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &uiPipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create UI pipeline layout");

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
    pipelineInfo.layout              = uiPipelineLayout;
    pipelineInfo.renderPass          = uiRenderPass->get();
    pipelineInfo.subpass             = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &uiPipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create UI graphics pipeline");
}

void Renderer::createUIBuffers(GpuDevice& gpu) {
    for (auto& ub : uiBuffers) ub.init(gpu);
}

// ---------------------------------------------------------------------------
// drawFrame
// ---------------------------------------------------------------------------

void Renderer::drawFrame(GpuDevice& gpu, Window& window, const Camera& camera, const World& world,
                        AssetManager& assets) {
    vkWaitForFences(gpu.device(), 1, &inFlightFence[currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(gpu.device(), swapchain->handle(),
                                            UINT64_MAX, imageAvailable[currentFrame],
                                            VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain(gpu, window);
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("Failed to acquire swapchain image");

    vkResetFences(gpu.device(), 1, &inFlightFence[currentFrame]);

    // Write camera matrices into this frame's UBO before recording commands.
    GlobalUBO uboData{};
    uboData.view         = camera.genViewMatrix();
    uboData.proj         = camera.genProjectionMatrix();
    math::Vec3 pos       = camera.getPosition();
    uboData.cameraPos[0] = pos.x;
    uboData.cameraPos[1] = pos.y;
    uboData.cameraPos[2] = pos.z;

    // Pack and upload lights from the world.
    const auto& worldLights = world.getLights();
    uint32_t numLights = static_cast<uint32_t>(worldLights.size());
    if (numLights > YOPE_MAX_LIGHTS) numLights = YOPE_MAX_LIGHTS;
    uboData.numLights = static_cast<int>(numLights);

    // Pack lights into variable-length float stream.
    std::vector<float> packedLights;
    math::Vec3 camDir = camera.getForward();  // Camera forward direction
    for (uint32_t i = 0; i < numLights; ++i) {
        auto lightData = packLight(worldLights[i], pos, camDir);
        packedLights.insert(packedLights.end(), lightData.begin(), lightData.end());
    }

    // Upload light data to the SSBO. Write only the actual packed size.
    if (!packedLights.empty()) {
        lightBuffers[currentFrame].write(packedLights.data(), packedLights.size() * sizeof(float));
    }

    // Upload UBO with updated numLights.
    uniformBuffers[currentFrame].write(&uboData, sizeof(GlobalUBO));

    // Build UI geometry for this frame before recording commands.
    if (uiManager_) {
        float sw = static_cast<float>(swapchain->extent().width);
        float sh = static_cast<float>(swapchain->extent().height);
        uiManager_->buildFrame(uiBuffers[currentFrame], sw, sh);
    }

    vkResetCommandBuffer(cmdBuffers[currentFrame], 0);
    recordCommandBuffer(cmdBuffers[currentFrame], imageIndex, world, assets);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = &imageAvailable[currentFrame];
    si.pWaitDstStageMask    = &waitStage;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &cmdBuffers[currentFrame];
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &renderFinished[imageIndex];

    if (vkQueueSubmit(gpu.graphicsQueue(), 1, &si, inFlightFence[currentFrame]) != VK_SUCCESS)
        throw std::runtime_error("Failed to submit draw command buffer");

    VkSwapchainKHR sc = swapchain->handle();
    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &renderFinished[imageIndex];
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &sc;
    pi.pImageIndices      = &imageIndex;

    result = vkQueuePresentKHR(gpu.presentQueue(), &pi);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || window.wasResized()) {
        window.clearResizedFlag();
        recreateSwapchain(gpu, window);
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to present swapchain image");
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES;
}
