#include "Renderer.h"
#include "Raytracer.h"
#include "Camera.h"
#ifdef YOPE_EDITOR
#include "rendering/ViewportTarget.h"
#endif
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
#include "ecs/Components.h"
#include "debug/Profiler.h"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <array>
#include <vector>
#include <cstring>
#include <iostream>

// ---------------------------------------------------------------------------
// packLightSource — packs ecs::LightSource into the same float stream as packLight().
// ---------------------------------------------------------------------------

static std::vector<float> packLightSource(const ecs::LightSource& ls) {
    auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    std::vector<float> r;
    r.push_back(static_cast<float>(ls.type));
    float cr = clamp01(ls.color[0] * ls.intensity);
    float cg = clamp01(ls.color[1] * ls.intensity);
    float cb = clamp01(ls.color[2] * ls.intensity);
    r.push_back(cr); r.push_back(cg); r.push_back(cb);
    if (ls.type == 0) {                               // Point
        r.push_back(ls.position[0]); r.push_back(ls.position[1]); r.push_back(ls.position[2]);
        r.push_back(ls.constant); r.push_back(ls.linear); r.push_back(ls.quadratic);
    } else if (ls.type == 1) {                        // Directional
        float len = std::sqrt(ls.direction[0]*ls.direction[0] +
                              ls.direction[1]*ls.direction[1] +
                              ls.direction[2]*ls.direction[2]);
        float inv = len > 1e-6f ? 1.f/len : 1.f;
        r.push_back(ls.direction[0]*inv); r.push_back(ls.direction[1]*inv); r.push_back(ls.direction[2]*inv);
    } else if (ls.type == 2) {                        // Spot
        r.push_back(ls.position[0]); r.push_back(ls.position[1]); r.push_back(ls.position[2]);
        float len = std::sqrt(ls.direction[0]*ls.direction[0] +
                              ls.direction[1]*ls.direction[1] +
                              ls.direction[2]*ls.direction[2]);
        float inv = len > 1e-6f ? 1.f/len : 1.f;
        r.push_back(ls.direction[0]*inv); r.push_back(ls.direction[1]*inv); r.push_back(ls.direction[2]*inv);
        r.push_back(ls.constant); r.push_back(ls.linear); r.push_back(ls.quadratic);
        r.push_back(std::cos(ls.innerConeAngle)); r.push_back(std::cos(ls.outerConeAngle));
    } else {                                          // Flash (type == 3)
        r.push_back(ls.constant); r.push_back(ls.linear); r.push_back(ls.quadratic);
        r.push_back(std::cos(ls.innerConeAngle)); r.push_back(std::cos(ls.outerConeAngle));
    }
    return r;
}

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
    createText3DPipeline(gpu.device());
    createText3DBuffers(gpu);
    createCommandPool(gpu);
    allocateCommandBuffers(gpu.device());
    createSyncObjects(gpu.device());

    // Raytracer (requires commandPool, so constructed last)
    createRaytracePass(gpu);
#ifdef YOPE_EDITOR
    createOffscreenGamePass(gpu);
    createOffscreenRaytracePass(gpu);
    createOffscreenUIPass(gpu);
#endif
    std::array<VkBuffer,     2> uboBuffs{},   lightBuffs{};
    std::array<VkDeviceSize, 2> uboSizes{},   lightSizes{};
    for (int i = 0; i < MAX_FRAMES; ++i) {
        uboBuffs[i]   = uniformBuffers[i].get();
        uboSizes[i]   = sizeof(GlobalUBO);
        lightBuffs[i] = lightBuffers[i].get();
        lightSizes[i] = lightBuffers[i].getSize();
    }
    raytracer_ = std::make_unique<Raytracer>(gpu, commandPool, *swapchain, raytracePass_->get(),
                                             uboBuffs, uboSizes, lightBuffs, lightSizes);
}

Renderer::~Renderer() {
    // Caller must have called waitIdle() before destroying.
}

VkDescriptorSetLayout Renderer::getTextureSetLayout() const {
    return textureSetLayout->get();
}

VkFormat Renderer::getDepthFormat() const {
    return depthBuffer->format();
}

void Renderer::waitIdle(GpuDevice& gpu) {
    vkDeviceWaitIdle(gpu.device());

    // Destroy raytracer before destroying commandPool / buffers it references.
    raytracer_.reset();
    raytracePass_.reset();

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

    // 3D text pipeline cleanup
    vkDestroyPipeline(gpu.device(), text3DPipeline_, nullptr);
    vkDestroyPipelineLayout(gpu.device(), text3DPipelineLayout_, nullptr);
    for (auto& tb : text3DBuffers_) tb.destroy(gpu.device());

#ifdef YOPE_EDITOR
    destroyOffscreenUIFramebuffer(gpu.device());
    offscreenUIPass_.reset();
#endif

    for (auto& ub : uniformBuffers) ub.destroy(gpu.device());
    for (auto& sb : lightBuffers) sb.destroy(gpu.device());
    descriptorPool.reset();  // all descriptor sets freed implicitly
    uboLayout.reset();

    depthBuffer.reset();
    renderPass.reset();
    swapchain.reset();
}

void Renderer::createRaytracePass(GpuDevice& gpu) {
    raytracePass_ = std::make_unique<RenderPass>(
        RenderPass::createRaytracePass(gpu.device(), swapchain->imageFormat()));
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
    raytracer_->onResize(gpu, swapchain->extent().width, swapchain->extent().height, commandPool);

    VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    renderFinished.resize(swapchain->imageCount());
    for (auto& sem : renderFinished)
        if (vkCreateSemaphore(gpu.device(), &si, nullptr, &sem) != VK_SUCCESS)
            throw std::runtime_error("Failed to recreate renderFinished semaphores");
}

// ---------------------------------------------------------------------------
// Command buffer recording
// ---------------------------------------------------------------------------

void Renderer::recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex, World& world,
                                  AssetManager& assets) {
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &bi);

    if (mode_ == RenderMode::RAYTRACE) {
        // ---- Raytracer path ----
        // Compute dispatch (outside any render pass), then blit into raytrace pass.
        raytracer_->dispatch(cmd, currentFrame);

        VkClearValue rtClear{};
        rtClear.color = {0.0f, 0.0f, 0.0f, 1.0f};

        VkRenderPassBeginInfo rtRpbi{};
        rtRpbi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rtRpbi.renderPass        = raytracePass_->get();
        rtRpbi.framebuffer       = raytracer_->framebuffers[imageIndex];
        rtRpbi.renderArea.offset = {0, 0};
        rtRpbi.renderArea.extent = swapchain->extent();
        rtRpbi.clearValueCount   = 1;
        rtRpbi.pClearValues      = &rtClear;

        vkCmdBeginRenderPass(cmd, &rtRpbi, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport rtVp{};
        rtVp.width    = static_cast<float>(swapchain->extent().width);
        rtVp.height   = static_cast<float>(swapchain->extent().height);
        rtVp.minDepth = 0.0f;
        rtVp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &rtVp);
        VkRect2D rtSc{ {0, 0}, swapchain->extent() };
        vkCmdSetScissor(cmd, 0, 1, &rtSc);

        raytracer_->recordBlit(cmd, currentFrame);
        vkCmdEndRenderPass(cmd);

    } else {
        // ---- Rasterizer path ----
        VkClearValue clearValues[2]{};
        clearValues[0].color        = {0.05f, 0.05f, 0.05f, 1.0f};
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

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
            0, 1, &descriptorSets[currentFrame], 0, nullptr);

        if (!world.debugPhysics) {
            auto& reg = world.getRegistry();
            // Query-only sentinel walk — measures pure ECS iteration cost over the
            // (Transform, MeshRenderer) archetypes, separated from the Vulkan record
            // work below. Combined with raster_cmdbuffer_record this answers
            // "is the cost the ECS walk or the GPU command submission?".
            {
                YOPE_PROF_SCOPE("view_meshrenderer", "render");
                static volatile int sink = 0;
                int acc = 0;
                for (auto [entity, tf, mr] : reg.view<Transform, ecs::MeshRenderer>()) {
                    (void)entity; (void)tf;
                    acc += mr.mesh ? 1 : 0;
                }
                sink = acc;
            }
            for (auto [entity, tf, mr] : reg.view<Transform, ecs::MeshRenderer>()) {
                if (!mr.mesh || !mr.mesh->transformReady) continue;

                math::Mat4 model = mr.mesh->modelMatrix;

                Texture* textureToUse = mr.mesh->texture ? mr.mesh->texture : assets.getDefaultTexture();
                VkDescriptorSet textureSet = textureToUse->getDescriptorSet();
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                    1, 1, &textureSet, 0, nullptr);

                struct PushConstants {
                    math::Mat4 model;
                    float      color[3];
                    int32_t    state;
                } push{};
                push.model    = model;
                push.color[0] = mr.mesh->color[0];
                push.color[1] = mr.mesh->color[1];
                push.color[2] = mr.mesh->color[2];
                push.state    = mr.mesh->state;

                vkCmdPushConstants(cmd, pipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0, 80, &push);

                mr.mesh->draw(cmd);
            }
        }

        if (world.debugPhysics) {
            VkDescriptorSet defaultTex = assets.getDefaultTexture()->getDescriptorSet();
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                1, 1, &defaultTex, 0, nullptr);

            for (const auto& dm : world.getDebugMeshes()) {
                if (!dm) continue;
                struct PushConstants { math::Mat4 model; float color[3]; int32_t state; } push{};
                push.model    = dm->modelMatrix;
                push.color[0] = 0.0f; push.color[1] = 1.0f; push.color[2] = 0.2f;
                push.state    = 0;
                vkCmdPushConstants(cmd, pipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0, 80, &push);
                dm->draw(cmd);
            }
        }

        // 3D world-space text — depth-tested against the scene, before pass end.
        recordText3D(cmd);

        vkCmdEndRenderPass(cmd);
    }

    // ---------------------------------------------------------------------------
    // UI render pass — runs after the 3D pass, loads color, no depth.
    // Combines UIManager labels (legacy script API) with ECS UI entities.
    // ---------------------------------------------------------------------------
    {
        std::vector<UIDrawCall> allUICalls;
        if (uiManager_) {
            const auto& dc = uiManager_->getDrawCalls();
            allUICalls.insert(allUICalls.end(), dc.begin(), dc.end());
        }
        allUICalls.insert(allUICalls.end(), ecsUIDrawCalls_.begin(), ecsUIDrawCalls_.end());

        if (!allUICalls.empty()) {
            VkRenderPassBeginInfo uiRpbi{};
            uiRpbi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            uiRpbi.renderPass        = uiRenderPass->get();
            uiRpbi.framebuffer       = uiFramebuffers[imageIndex];
            uiRpbi.renderArea.offset = {0, 0};
            uiRpbi.renderArea.extent = swapchain->extent();
            uiRpbi.clearValueCount   = 0;   // LOAD — no clear

            vkCmdBeginRenderPass(cmd, &uiRpbi, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, uiPipeline);

            // ui.frag statically declares a sampler at set 0 — always bind a
            // compatible set so solid-color draws (dc.texture==VK_NULL_HANDLE)
            // don't trigger a validation error from the previous 3D pipeline's set.
            if (uiManager_) {
                VkDescriptorSet dummy = uiManager_->dummyDescSet();
                if (dummy != VK_NULL_HANDLE)
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        uiPipelineLayout, 0, 1, &dummy, 0, nullptr);
            }

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

            for (const auto& dc : allUICalls) {
                if (dc.texture != VK_NULL_HANDLE) {
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        uiPipelineLayout, 0, 1, &dc.texture, 0, nullptr);
                }
                struct { int32_t state; float distanceRange; } uipush{ dc.state, dc.distanceRange };
                vkCmdPushConstants(cmd, uiPipelineLayout,
                    VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(uipush), &uipush);
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
// ECS UI geometry builder
// ---------------------------------------------------------------------------

void Renderer::buildECSUIGeometry(UIBuffer& buf, World& world, float sw, float sh) {
    ecsUIDrawCalls_.clear();
    if (!uiManager_) buf.begin();   // UIManager calls begin(); we must if it's absent

    // Default character height (reference px) for UIText with no explicit displayPx.
    constexpr int kDefaultUITextPx = 54;

    auto& reg = world.getRegistry();

    // Collect all visible UI entities and sort by depth (ascending = back-to-front).
    struct UIEntry { ecs::Entity e; int depth; };
    std::vector<UIEntry> entries;
    for (auto [e, uiTf] : reg.view<ecs::UITransform>()) {
        if (uiTf.visible) entries.push_back({e, uiTf.depth});
    }
    std::stable_sort(entries.begin(), entries.end(),
                     [](const UIEntry& a, const UIEntry& b){ return a.depth < b.depth; });

    for (auto& entry : entries) {
        ecs::Entity e    = entry.e;
        const auto* uiTf = reg.get<ecs::UITransform>(e);
        if (!uiTf) continue;

        math::Vec2 mn{uiTf->minX, uiTf->minY};
        math::Vec2 mx{uiTf->maxX, uiTf->maxY};

        if (const auto* bg = reg.get<ecs::UIBackground>(e)) {
            Background tmp(mn, mx, {bg->r, bg->g, bg->b, bg->a}, uiTf->depth);
            tmp.buildMesh(buf, sw, sh);
            if (tmp.drawCall.indexCount > 0) ecsUIDrawCalls_.push_back(tmp.drawCall);

        } else if (const auto* cbg = reg.get<ecs::UICurvedBackground>(e)) {
            CurvedBackground tmp(mn, mx, {cbg->r, cbg->g, cbg->b, cbg->a},
                                 uiTf->depth, cbg->curvature);
            tmp.buildMesh(buf, sw, sh);
            if (tmp.drawCall.indexCount > 0) ecsUIDrawCalls_.push_back(tmp.drawCall);

        } else if (const auto* tbg = reg.get<ecs::UITexturedBackground>(e)) {
            if (tbg->texture) {
                TexturedBackground tmp(mn, mx,
                                       {tbg->tintR, tbg->tintG, tbg->tintB, tbg->tintA},
                                       uiTf->depth, tbg->texture);
                tmp.buildMesh(buf, sw, sh);
                if (tmp.drawCall.indexCount > 0) ecsUIDrawCalls_.push_back(tmp.drawCall);
            }

        } else if (const auto* ut = reg.get<ecs::UIText>(e)) {
            if (uiManager_ && ut->fontPath[0] && ut->text[0]) {
                // Bake the atlas at a FIXED reference pixel size, independent of the
                // render target. TextBox scales glyphs to the actual screen height,
                // so layout is resolution-independent and the atlas is cached/stable
                // instead of being re-baked per viewport size (the old sh-derived
                // size churned a new atlas every time the viewport resized).
                int displayPx = (ut->displayPx > 0) ? ut->displayPx : kDefaultUITextPx;
                TextAtlas* atlas = uiManager_->loadAtlas(ut->fontPath, displayPx);
                if (atlas) {
                    Background boundsBox(mn, mx, {0,0,0,0}, 0);
                    TextBox tb(&boundsBox, atlas, ut->text, uiTf->depth,
                               displayPx, static_cast<Alignment>(ut->alignment));
                    tb.setColor(ut->cr, ut->cg, ut->cb, ut->ca);
                    tb.buildMesh(buf, sw, sh);
                    if (tb.drawCall.indexCount > 0) ecsUIDrawCalls_.push_back(tb.drawCall);
                }
            }
        }
    }
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

    // RGB: standard pre-multiplied alpha blend.
    // Alpha: PRESERVE destination alpha (ONE_MINUS_SRC_ALPHA instead of ZERO) so the
    // viewport texture alpha stays at 1.0 from the 3D pass.
    // Without this, text coverage values written to alpha cause empty glyph-box pixels
    // to be composited against the ImGui window background in the editor, producing
    // visible dark rectangles around every character cell.
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable         = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
    // Alpha channel: output = max(srcAlpha, dstAlpha * (1 - srcAlpha)).
    // With srcFactor=ONE and dstFactor=ONE_MINUS_SRC_ALPHA the viewport texture
    // always ends up with alpha=1 where UI is drawn, preventing ImGui from
    // darkening those pixels when displaying the offscreen texture as an image.
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;
    blendAttachment.colorWriteMask      =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments    = &blendAttachment;

    // Push constant: int state + float distanceRange (8 bytes), fragment stage only.
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset     = 0;
    pushRange.size       = 8;

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
// 3D world-space text
// ---------------------------------------------------------------------------

void Renderer::createText3DBuffers(GpuDevice& gpu) {
    for (auto& tb : text3DBuffers_) tb.init(gpu);
}

void Renderer::createText3DPipeline(VkDevice device) {
    ShaderModule vert(device, std::string(YOPE_SHADER_DIR) + "/text3d.vert.spv");
    ShaderModule frag(device, std::string(YOPE_SHADER_DIR) + "/text3d.frag.spv");

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert.get();
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag.get();
    stages[1].pName  = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(Text3DVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3]{};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(Text3DVertex, x) };
    attrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(Text3DVertex, u) };
    attrs[2] = { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Text3DVertex, r) };

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
    rasterizer.cullMode    = VK_CULL_MODE_NONE;   // text quads are double-sided
    rasterizer.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth: TEST against scene geometry (so text is occluded), but do NOT WRITE —
    // blended AA fringes would otherwise punch depth and self-occlude into halos.
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable  = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp   = VK_COMPARE_OP_LESS;
    depthStencil.stencilTestEnable= VK_FALSE;

    // Same blend as the UI pass: premultiplied-alpha color, dst-alpha preserved so
    // the editor's offscreen viewport texture keeps alpha=1 where text is drawn.
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable         = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;
    blendAttachment.colorWriteMask      =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments    = &blendAttachment;

    // Push: mat4 model + float distanceRange + int billboard (72 bytes), both stages.
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset     = 0;
    pushRange.size       = 72;

    // Set 0 = GlobalUBO (shared with the mesh pipeline), Set 1 = MSDF atlas sampler.
    VkDescriptorSetLayout setLayouts[2] = { uboLayout->get(), textureSetLayout->get() };
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount         = 2;
    layoutInfo.pSetLayouts            = setLayouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushRange;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &text3DPipelineLayout_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create text3D pipeline layout");

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
    pipelineInfo.layout              = text3DPipelineLayout_;
    // Created against the main render pass; reused in the editor's offscreen game
    // pass too (the two are render-pass-compatible, like the mesh pipeline).
    pipelineInfo.renderPass          = renderPass->get();
    pipelineInfo.subpass             = 0;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &text3DPipeline_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create text3D pipeline");
}

void Renderer::buildECSText3DGeometry(Text3DBuffer& buf, World& world) {
    ecsText3DDrawCalls_.clear();
    buf.begin();
    if (!uiManager_) return;

    auto& reg = world.getRegistry();
    std::vector<Text3DVertex> verts;
    std::vector<uint32_t>     indices;

    for (auto [e, tf, t] : reg.view<Transform, ecs::TextLabel3D>()) {
        (void)e;
        if (!t.fontPath[0] || !t.text[0]) continue;
        TextAtlas* atlas = uiManager_->loadAtlas(t.fontPath);
        if (!atlas) continue;

        float s = t.sizeMeters;

        // Measure the (single-line) width, then center horizontally on the anchor.
        float width = 0.0f;
        for (const char* p = t.text; *p; ++p) {
            const GlyphInfo* g = atlas->glyph(*p);
            if (g) width += g->advance * s;
        }
        float penX  = -0.5f * width;
        float baseY = -0.5f * atlas->ascender() * s;   // roughly vertical-center the cap height

        verts.clear();
        indices.clear();
        for (const char* p = t.text; *p; ++p) {
            const GlyphInfo* g = atlas->glyph(*p);
            if (!g) continue;
            if (g->hasQuad) {
                // planeBounds are em, Y-up, baseline origin → local meters, Y-up.
                float xMin = penX + g->planeL * s;
                float xMax = penX + g->planeR * s;
                float yBot = baseY + g->planeB * s;
                float yTop = baseY + g->planeT * s;   // top → atlas v0
                uint32_t base = static_cast<uint32_t>(verts.size());
                verts.push_back({ xMin, yTop, 0.0f, g->u0, g->v0, t.cr, t.cg, t.cb, t.ca });
                verts.push_back({ xMax, yTop, 0.0f, g->u1, g->v0, t.cr, t.cg, t.cb, t.ca });
                verts.push_back({ xMax, yBot, 0.0f, g->u1, g->v1, t.cr, t.cg, t.cb, t.ca });
                verts.push_back({ xMin, yBot, 0.0f, g->u0, g->v1, t.cr, t.cg, t.cb, t.ca });
                indices.insert(indices.end(), { base, base+1, base+2, base, base+2, base+3 });
            }
            penX += g->advance * s;
        }
        if (verts.empty()) continue;

        auto r = buf.push(verts.data(), static_cast<uint32_t>(verts.size()),
                          indices.data(), static_cast<uint32_t>(indices.size()));
        Text3DDrawCall dc{};
        dc.indexCount    = r.indexCount;
        dc.indexOffset   = r.indexOffset;
        dc.vertexOffset  = r.vertexOffset;
        dc.atlas         = atlas->descriptorSet();
        dc.distanceRange = atlas->distanceRange();
        dc.billboard     = t.billboard;
        dc.model         = tf.getModelMatrix();
        ecsText3DDrawCalls_.push_back(dc);
    }
}

void Renderer::recordText3D(VkCommandBuffer cmd) {
    if (ecsText3DDrawCalls_.empty()) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, text3DPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, text3DPipelineLayout_,
        0, 1, &descriptorSets[currentFrame], 0, nullptr);

    VkBuffer     vb  = text3DBuffers_[currentFrame].vertexBuffer();
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &off);
    vkCmdBindIndexBuffer(cmd, text3DBuffers_[currentFrame].indexBuffer(), 0, VK_INDEX_TYPE_UINT32);

    for (const auto& dc : ecsText3DDrawCalls_) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, text3DPipelineLayout_,
            1, 1, &dc.atlas, 0, nullptr);
        struct { math::Mat4 model; float distanceRange; int32_t billboard; }
            push{ dc.model, dc.distanceRange, dc.billboard };
        vkCmdPushConstants(cmd, text3DPipelineLayout_,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        vkCmdDrawIndexed(cmd, dc.indexCount, 1, dc.indexOffset, dc.vertexOffset, 0);
    }
}

// ---------------------------------------------------------------------------
// drawFrame
// ---------------------------------------------------------------------------

void Renderer::drawFrame(GpuDevice& gpu, Window& window, const Camera& camera, World& world,
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

    // Pack lights from ECS registry.
    std::vector<float> packedLights;
    int numLights = 0;
    {
        YOPE_PROF_SCOPE("view_lightsource", "render");
        for (auto [entity, ls] : world.getRegistry().view<ecs::LightSource>()) {
            if (numLights >= static_cast<int>(YOPE_MAX_LIGHTS)) break;
            auto lightData = packLightSource(ls);
            packedLights.insert(packedLights.end(), lightData.begin(), lightData.end());
            ++numLights;
        }
    }
    uboData.numLights = numLights;

    // Upload light data to the SSBO. Write only the actual packed size.
    if (!packedLights.empty()) {
        lightBuffers[currentFrame].write(packedLights.data(), packedLights.size() * sizeof(float));
    }

    // Upload UBO with updated numLights.
    uniformBuffers[currentFrame].write(&uboData, sizeof(GlobalUBO));

    // Pack world geometry into the raytracer SSBO (only in raytrace mode).
    if (mode_ == RenderMode::RAYTRACE)
        raytracer_->prepareFrame(currentFrame, world);

    // Build UI geometry for this frame before recording commands.
    {
        float sw = static_cast<float>(swapchain->extent().width);
        float sh = static_cast<float>(swapchain->extent().height);
        if (uiManager_) uiManager_->buildFrame(uiBuffers[currentFrame], sw, sh);
        buildECSUIGeometry(uiBuffers[currentFrame], world, sw, sh);
        buildECSText3DGeometry(text3DBuffers_[currentFrame], world);
    }

    vkResetCommandBuffer(cmdBuffers[currentFrame], 0);
    {
        YOPE_PROF_SCOPE("raster_cmdbuffer_record", "render");
        recordCommandBuffer(cmdBuffers[currentFrame], imageIndex, world, assets);
    }

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

// ---------------------------------------------------------------------------
// Editor offscreen path (#ifdef YOPE_EDITOR)
// ---------------------------------------------------------------------------

#ifdef YOPE_EDITOR
void Renderer::createOffscreenGamePass(GpuDevice& gpu) {
    offscreenGamePass_ = std::make_unique<RenderPass>(
        RenderPass::createOffscreenGamePass(gpu.device(),
                                            swapchain->imageFormat(),
                                            depthBuffer->format()));
}

void Renderer::createOffscreenRaytracePass(GpuDevice& gpu) {
    offscreenRaytracePass_ = std::make_unique<RenderPass>(
        RenderPass::createOffscreenRaytracePass(gpu.device(), swapchain->imageFormat()));
}

void Renderer::createOffscreenUIPass(GpuDevice& gpu) {
    offscreenUIPass_ = std::make_unique<RenderPass>(
        RenderPass::createOffscreenUIPass(gpu.device(), swapchain->imageFormat()));
}

void Renderer::destroyOffscreenUIFramebuffer(VkDevice device) {
    for (int i = 0; i < MAX_FRAMES; ++i) {
        if (offscreenUIFb_[i] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, offscreenUIFb_[i], nullptr);
            offscreenUIFb_[i] = VK_NULL_HANDLE;
        }
        offscreenUIView_[i] = VK_NULL_HANDLE;
    }
    offscreenUIW_ = 0;
    offscreenUIH_ = 0;
}

void Renderer::notifyViewportResizing(VkDevice device) {
    // EditorApp's resize path syncs the device first, so it's safe to destroy
    // the framebuffers here; the next beginFrameForEditor recreates them lazily
    // against the new (post-resize) color views from ViewportTarget.
    destroyOffscreenUIFramebuffer(device);
}

void Renderer::recreateOffscreenUIFramebufferIfNeeded(VkDevice device,
                                                      ViewportTarget& vt) {
    // Recreate only the current frame's slot, and only when its cached color
    // view (or the size) changed — so steady-state rendering does no per-frame
    // framebuffer churn even though vt.colorView() ping-pongs between frames.
    VkImageView view = vt.colorView();
    if (offscreenUIFb_[currentFrame] != VK_NULL_HANDLE &&
        offscreenUIView_[currentFrame] == view &&
        offscreenUIW_ == vt.width() &&
        offscreenUIH_ == vt.height()) {
        return;
    }
    if (offscreenUIFb_[currentFrame] != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device, offscreenUIFb_[currentFrame], nullptr);
        offscreenUIFb_[currentFrame] = VK_NULL_HANDLE;
    }

    VkFramebufferCreateInfo fi{};
    fi.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fi.renderPass      = offscreenUIPass_->get();
    fi.attachmentCount = 1;
    fi.pAttachments    = &view;
    fi.width           = vt.width();
    fi.height          = vt.height();
    fi.layers          = 1;
    if (vkCreateFramebuffer(device, &fi, nullptr, &offscreenUIFb_[currentFrame]) != VK_SUCCESS)
        throw std::runtime_error("Failed to create offscreen UI framebuffer");
    offscreenUIView_[currentFrame] = view;
    offscreenUIW_ = vt.width();
    offscreenUIH_ = vt.height();
}

VkRenderPass Renderer::getOffscreenGamePass() const {
    return offscreenGamePass_ ? offscreenGamePass_->get() : VK_NULL_HANDLE;
}

VkDescriptorSetLayout Renderer::getUBOSetLayout() const {
    return uboLayout ? uboLayout->get() : VK_NULL_HANDLE;
}

VkRenderPass Renderer::getOffscreenRaytracePass() const {
    return offscreenRaytracePass_ ? offscreenRaytracePass_->get() : VK_NULL_HANDLE;
}

uint32_t Renderer::beginFrameForEditor(GpuDevice& gpu, Window& window,
                                       const Camera& camera, World& world,
                                       AssetManager& assets,
                                       ViewportTarget& vt) {
    // Select this frame's double-buffered viewport images. Every vt.xxx()
    // accessor below — plus the IdBufferPass record and the ImGui::Image in the
    // panel, which run later this tick — resolves to this slot. Waiting on this
    // slot's fence guarantees its prior use (two ticks ago) has fully retired.
    vt.setActiveFrame(currentFrame);
    vkWaitForFences(gpu.device(), 1, &inFlightFence[currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(gpu.device(), swapchain->handle(),
                                            UINT64_MAX, imageAvailable[currentFrame],
                                            VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain(gpu, window);
        // Return sentinel — EditorApp must check and skip ImGui recording this frame.
        return UINT32_MAX;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("beginFrameForEditor: failed to acquire image");

    vkResetFences(gpu.device(), 1, &inFlightFence[currentFrame]);

    // Upload per-frame data (UBO + light SSBO)
    GlobalUBO uboData{};
    uboData.view         = camera.genViewMatrix();
    uboData.proj         = camera.genProjectionMatrix();
    math::Vec3 pos       = camera.getPosition();
    uboData.cameraPos[0] = pos.x; uboData.cameraPos[1] = pos.y; uboData.cameraPos[2] = pos.z;

    std::vector<float> packedLights;
    int numLights = 0;
    for (auto [entity, ls] : world.getRegistry().view<ecs::LightSource>()) {
        if (numLights >= static_cast<int>(YOPE_MAX_LIGHTS)) break;
        auto d = packLightSource(ls);
        packedLights.insert(packedLights.end(), d.begin(), d.end());
        ++numLights;
    }
    uboData.numLights = numLights;
    if (!packedLights.empty())
        lightBuffers[currentFrame].write(packedLights.data(), packedLights.size() * sizeof(float));
    uniformBuffers[currentFrame].write(&uboData, sizeof(GlobalUBO));

    VkCommandBuffer cmd = cmdBuffers[currentFrame];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &bi);
    
    if (mode_ == RenderMode::RAYTRACE && raytracer_ && offscreenRaytracePass_
        && vt.raytraceFramebuffer() != VK_NULL_HANDLE) {
        // ---- Raytrace path ----
        raytracer_->prepareFrame(currentFrame, world);
        raytracer_->dispatch(cmd, currentFrame);

        VkClearValue rtClear{};
        rtClear.color = {0.0f, 0.0f, 0.0f, 1.0f};

        VkRenderPassBeginInfo rpbi{};
        rpbi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpbi.renderPass        = offscreenRaytracePass_->get();
        rpbi.framebuffer       = vt.raytraceFramebuffer();
        rpbi.renderArea.offset = {0, 0};
        rpbi.renderArea.extent = {vt.width(), vt.height()};
        rpbi.clearValueCount   = 1;
        rpbi.pClearValues      = &rtClear;
        
        vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport vp{};
        vp.width = static_cast<float>(vt.width());
        vp.height = static_cast<float>(vt.height());
        vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D sc2{{0,0},{vt.width(),vt.height()}};
        vkCmdSetScissor(cmd, 0, 1, &sc2);

        raytracer_->recordBlit(cmd, currentFrame);
        vkCmdEndRenderPass(cmd);
    } else {
        // ---- Raster path ----
        // Build 3D-text geometry before the pass (under the structure lock, like
        // the UI build) so play-mode archetype migrations don't race the view.
        {
            auto _lk = world.lockStructure();
            buildECSText3DGeometry(text3DBuffers_[currentFrame], world);
        }

        VkClearValue clearValues[2]{};
        clearValues[0].color        = {0.05f, 0.05f, 0.05f, 1.0f};
        clearValues[1].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo rpbi{};
        rpbi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpbi.renderPass        = offscreenGamePass_->get();
        rpbi.framebuffer       = vt.framebuffer();
        rpbi.renderArea.offset = {0, 0};
        rpbi.renderArea.extent = {vt.width(), vt.height()};
        rpbi.clearValueCount   = 2;
        rpbi.pClearValues      = clearValues;

        vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        VkViewport vp{};
        vp.width    = static_cast<float>(vt.width());
        vp.height   = static_cast<float>(vt.height());
        vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D scissor{{0, 0}, {vt.width(), vt.height()}};
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout, 0, 1, &descriptorSets[currentFrame], 0, nullptr);
        
        
        auto& reg = world.getRegistry();
        if (!world.debugPhysics) {
            for (auto [entity, tf, mr] : reg.view<Transform, ecs::MeshRenderer>()) {
                if (!mr.mesh || !mr.mesh->transformReady) continue;

                Texture* tex = mr.mesh->texture ? mr.mesh->texture : assets.getDefaultTexture();
                VkDescriptorSet texSet = tex->getDescriptorSet();
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipelineLayout, 1, 1, &texSet, 0, nullptr);

                struct PushConstants { math::Mat4 model; float color[3]; int32_t state; } push{};
                push.model    = mr.mesh->modelMatrix;
                push.color[0] = mr.mesh->color[0]; push.color[1] = mr.mesh->color[1]; push.color[2] = mr.mesh->color[2];
                push.state    = mr.mesh->state;
                vkCmdPushConstants(cmd, pipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80, &push);
                mr.mesh->draw(cmd);
            }
        }

        if (world.debugPhysics) {
            VkDescriptorSet defaultTex = assets.getDefaultTexture()->getDescriptorSet();
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                1, 1, &defaultTex, 0, nullptr);
            for (const auto& dm : world.getDebugMeshes()) {
                if (!dm) continue;
                struct PushConstants { math::Mat4 model; float color[3]; int32_t state; } push{};
                push.model    = dm->modelMatrix;
                push.color[0] = 0.0f; push.color[1] = 1.0f; push.color[2] = 0.2f;
                push.state    = 0;
                vkCmdPushConstants(cmd, pipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80, &push);
                dm->draw(cmd);
            }
        }

        // 3D world-space text — depth-tested against the scene, before pass end.
        recordText3D(cmd);

        vkCmdEndRenderPass(cmd);
    }

    // ---- Offscreen UI pass ----
    // Builds UIManager + ECS UI geometry and composites onto the viewport texture.
    if (offscreenUIPass_) {
        float uw = static_cast<float>(vt.width());
        float uh = static_cast<float>(vt.height());
        // Build UI geometry under the structure lock so concurrent archetype
        // migrations in the physics thread (Sleeping tag additions) don't race
        // with the view iterator reading archetypes_ during play mode.
        {
            auto _lk = world.lockStructure();
            uiBuffers[currentFrame].begin();
            if (uiManager_) uiManager_->buildFrame(uiBuffers[currentFrame], uw, uh);
            buildECSUIGeometry(uiBuffers[currentFrame], world, uw, uh);
        }

        std::vector<UIDrawCall> allUICalls;
        if (uiManager_) {
            const auto& dc = uiManager_->getDrawCalls();
            allUICalls.insert(allUICalls.end(), dc.begin(), dc.end());
        }
        allUICalls.insert(allUICalls.end(), ecsUIDrawCalls_.begin(), ecsUIDrawCalls_.end());

        // Always run the offscreen UI pass — skipping it when allUICalls is
        // momentarily empty (e.g. first play frame) causes visible flicker.
        {
            recreateOffscreenUIFramebufferIfNeeded(gpu.device(), vt);

            VkRenderPassBeginInfo uiRpbi{};
            uiRpbi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            uiRpbi.renderPass        = offscreenUIPass_->get();
            uiRpbi.framebuffer       = offscreenUIFb_[currentFrame];
            uiRpbi.renderArea.offset = {0, 0};
            uiRpbi.renderArea.extent = {vt.width(), vt.height()};
            uiRpbi.clearValueCount   = 0;

            vkCmdBeginRenderPass(cmd, &uiRpbi, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, uiPipeline);

            if (uiManager_) {
                VkDescriptorSet dummy = uiManager_->dummyDescSet();
                if (dummy != VK_NULL_HANDLE)
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        uiPipelineLayout, 0, 1, &dummy, 0, nullptr);
            }

            VkViewport uiVp{};
            uiVp.width    = uw;
            uiVp.height   = uh;
            uiVp.minDepth = 0.0f;
            uiVp.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &uiVp);
            VkRect2D uiSc{{0, 0}, {vt.width(), vt.height()}};
            vkCmdSetScissor(cmd, 0, 1, &uiSc);

            const UIBuffer& ubuf = uiBuffers[currentFrame];
            VkBuffer vb         = ubuf.vertexBuffer();
            VkDeviceSize vbOffset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &vbOffset);
            vkCmdBindIndexBuffer(cmd, ubuf.indexBuffer(), 0, VK_INDEX_TYPE_UINT32);

            for (const auto& dc : allUICalls) {
                if (dc.texture != VK_NULL_HANDLE) {
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        uiPipelineLayout, 0, 1, &dc.texture, 0, nullptr);
                }
                struct { int32_t state; float distanceRange; } uipush{ dc.state, dc.distanceRange };
                vkCmdPushConstants(cmd, uiPipelineLayout,
                    VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(uipush), &uipush);
                vkCmdDrawIndexed(cmd, dc.indexCount, 1, dc.indexOffset,
                                 dc.vertexOffset, 0);
            }

            vkCmdEndRenderPass(cmd);
        }

        // Make the offscreen UI pass's color writes available + visible to ImGui's
        // fragment-shader sample of the same viewport image. The render pass leaves
        // the image in SHADER_READ_ONLY_OPTIMAL but its implicit exit dependency
        // doesn't chain the color write to a shader read, so without this barrier
        // the sample races the UI writes — seen as chunks of the UI blinking in/out
        // (worse at higher frame rates). Done as an explicit barrier rather than a
        // render-pass exit dependency because uiPipeline is shared with the swapchain
        // UI pass and extra dependencies would break render-pass compatibility.
        VkImageMemoryBarrier uiToSample{};
        uiToSample.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        uiToSample.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        uiToSample.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        uiToSample.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        uiToSample.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        uiToSample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        uiToSample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        uiToSample.image               = vt.colorImage();
        uiToSample.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &uiToSample);
    }

    // Command buffer stays open — ImGuiBackend::render() records the ImGui pass next.
    return imageIndex;
}

bool Renderer::endFrameForEditor(GpuDevice& gpu, Window& window, uint32_t imageIndex) {
    vkEndCommandBuffer(cmdBuffers[currentFrame]);

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
        throw std::runtime_error("endFrameForEditor: failed to submit");

    VkSwapchainKHR sc = swapchain->handle();
    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &renderFinished[imageIndex];
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &sc;
    pi.pImageIndices      = &imageIndex;
    VkResult result = vkQueuePresentKHR(gpu.presentQueue(), &pi);

    currentFrame = (currentFrame + 1) % MAX_FRAMES;

    bool swapchainRecreated = false;
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || window.wasResized()) {
        window.clearResizedFlag();
        recreateSwapchain(gpu, window);
        swapchainRecreated = true;
    }
    return swapchainRecreated;
}

void Renderer::notifySwapchainRecreated(GpuDevice& gpu, Window& window) {
    recreateSwapchain(gpu, window);
}
#endif
