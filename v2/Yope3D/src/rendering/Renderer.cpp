#include "Renderer.h"
#include "gpu/GpuDevice.h"
#include "gpu/Swapchain.h"
#include "gpu/RenderPass.h"
#include "gpu/ShaderModule.h"
#include "platform/Window.h"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <array>
#include <cstring>

// ---------------------------------------------------------------------------
// Default triangle (RGB, NDC coordinates)
// ---------------------------------------------------------------------------

static const std::vector<Vertex> kDefaultVertices = {
    {{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},
    {{ 0.5f,  0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    {{-0.5f,  0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    {{ 0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}}
};
static const std::vector<uint32_t> kDefaultIndices = { 
    0,1,2,
    0,1,3
};

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Renderer::Renderer(GpuDevice& gpu, Window& window) {
    swapchain = std::make_unique<Swapchain>(gpu, window);
    createRenderPass(gpu);
    createPipeline(gpu.device());
    createFramebuffers(gpu.device());
    createCommandPool(gpu);
    allocateCommandBuffers(gpu.device());
    createSyncObjects(gpu.device());
    setDefaultMesh(gpu);
}

Renderer::~Renderer() {
    // Caller must have called waitIdle() before destroying.
    // All Vulkan objects are destroyed here in reverse construction order.
}

void Renderer::waitIdle(GpuDevice& gpu) {
    vkDeviceWaitIdle(gpu.device());

    destroyMesh(gpu.device());

    for (int i = 0; i < MAX_FRAMES; ++i) {
        vkDestroySemaphore(gpu.device(), imageAvailable[i], nullptr);
        vkDestroyFence(gpu.device(), inFlightFence[i], nullptr);
    }
    for (auto& sem : renderFinished)
        vkDestroySemaphore(gpu.device(), sem, nullptr);
    renderFinished.clear();

    vkDestroyCommandPool(gpu.device(), commandPool, nullptr);

    destroyFramebuffers(gpu.device());

    vkDestroyPipeline(gpu.device(), pipeline, nullptr);
    vkDestroyPipelineLayout(gpu.device(), pipelineLayout, nullptr);

    renderPass.reset();
    swapchain.reset();
}

// ---------------------------------------------------------------------------
// Render pass
// ---------------------------------------------------------------------------

void Renderer::createRenderPass(GpuDevice& gpu) {
    renderPass = std::make_unique<RenderPass>(gpu.device(), swapchain->imageFormat());
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

    // Vertex input — one binding, two attributes (position + color)
    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0].location = 0;
    attrs[0].binding  = 0;
    attrs[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset   = offsetof(Vertex, position);
    attrs[1].location = 1;
    attrs[1].binding  = 0;
    attrs[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset   = offsetof(Vertex, color);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount   = 1;
    vertexInput.pVertexBindingDescriptions      = &binding;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Viewport and scissor are dynamic — set each frame to match swapchain extent.
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
    colorBlend.sType             = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount   = 1;
    colorBlend.pAttachments      = &blendAttachment;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
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
        VkFramebufferCreateInfo fi{};
        fi.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass      = renderPass->get();
        fi.attachmentCount = 1;
        fi.pAttachments    = &views[i];
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
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT; // start signaled so frame 0 doesn't deadlock

    for (int i = 0; i < MAX_FRAMES; ++i) {
        if (vkCreateSemaphore(device, &si, nullptr, &imageAvailable[i]) != VK_SUCCESS ||
            vkCreateFence(device, &fi, nullptr, &inFlightFence[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create sync objects");
    }

    // One renderFinished semaphore per swapchain image.  imageAvailable is per-frame
    // (safe because the fence ensures the prior submit consuming it is done before reuse),
    // but renderFinished is waited on by vkQueuePresentKHR, which holds it until the image
    // is re-acquired.  In MAILBOX mode images can be skipped, so frame-indexed reuse races.
    renderFinished.resize(swapchain->imageCount());
    for (auto& sem : renderFinished)
        if (vkCreateSemaphore(device, &si, nullptr, &sem) != VK_SUCCESS)
            throw std::runtime_error("Failed to create renderFinished semaphore");
}

// ---------------------------------------------------------------------------
// Mesh upload
// ---------------------------------------------------------------------------

void Renderer::setDefaultMesh(GpuDevice& gpu) {
    setMesh(gpu, kDefaultVertices, kDefaultIndices);
}

void Renderer::setMesh(GpuDevice& gpu,
                       const std::vector<Vertex>&   vertices,
                       const std::vector<uint32_t>& indices)
{
    vkDeviceWaitIdle(gpu.device());
    destroyMesh(gpu.device());

    uploadViaStaging(gpu,
        vertices.data(), sizeof(Vertex) * vertices.size(),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        vertexBuffer, vertexMemory);

    uploadViaStaging(gpu,
        indices.data(), sizeof(uint32_t) * indices.size(),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        indexBuffer, indexMemory);

    indexCount = static_cast<uint32_t>(indices.size());
}

void Renderer::destroyMesh(VkDevice device) {
    if (indexBuffer  != VK_NULL_HANDLE) { vkDestroyBuffer(device, indexBuffer,  nullptr); indexBuffer  = VK_NULL_HANDLE; }
    if (indexMemory  != VK_NULL_HANDLE) { vkFreeMemory(device, indexMemory,  nullptr); indexMemory  = VK_NULL_HANDLE; }
    if (vertexBuffer != VK_NULL_HANDLE) { vkDestroyBuffer(device, vertexBuffer, nullptr); vertexBuffer = VK_NULL_HANDLE; }
    if (vertexMemory != VK_NULL_HANDLE) { vkFreeMemory(device, vertexMemory, nullptr); vertexMemory = VK_NULL_HANDLE; }
}

// ---------------------------------------------------------------------------
// Buffer utilities
// ---------------------------------------------------------------------------

uint32_t Renderer::findMemoryType(VkPhysicalDevice pd, uint32_t filter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(pd, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        if ((filter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("Failed to find suitable memory type");
}

void Renderer::createBuffer(GpuDevice& gpu, VkDeviceSize size,
                            VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                            VkBuffer& buf, VkDeviceMemory& mem)
{
    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = size;
    bi.usage       = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(gpu.device(), &bi, nullptr, &buf) != VK_SUCCESS)
        throw std::runtime_error("Failed to create buffer");

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(gpu.device(), buf, &req);

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = findMemoryType(gpu.physicalDevice(), req.memoryTypeBits, props);
    if (vkAllocateMemory(gpu.device(), &ai, nullptr, &mem) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate buffer memory");

    vkBindBufferMemory(gpu.device(), buf, mem, 0);
}

void Renderer::uploadViaStaging(GpuDevice& gpu, const void* data, VkDeviceSize size,
                                VkBufferUsageFlags dstUsage,
                                VkBuffer& outBuffer, VkDeviceMemory& outMemory)
{
    VkBuffer       stagingBuf;
    VkDeviceMemory stagingMem;
    createBuffer(gpu, size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuf, stagingMem);

    void* mapped;
    vkMapMemory(gpu.device(), stagingMem, 0, size, 0, &mapped);
    memcpy(mapped, data, size);
    vkUnmapMemory(gpu.device(), stagingMem);

    createBuffer(gpu, size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | dstUsage,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        outBuffer, outMemory);

    copyBuffer(gpu, stagingBuf, outBuffer, size);

    vkDestroyBuffer(gpu.device(), stagingBuf, nullptr);
    vkFreeMemory(gpu.device(), stagingMem, nullptr);
}

VkCommandBuffer Renderer::beginOneTimeCommands(VkDevice device) {
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandPool        = commandPool;
    ai.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &ai, &cmd);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

void Renderer::endOneTimeCommands(VkDevice device, VkQueue queue, VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
}

void Renderer::copyBuffer(GpuDevice& gpu, VkBuffer src, VkBuffer dst, VkDeviceSize size) {
    VkCommandBuffer cmd = beginOneTimeCommands(gpu.device());
    VkBufferCopy region{ 0, 0, size };
    vkCmdCopyBuffer(cmd, src, dst, 1, &region);
    endOneTimeCommands(gpu.device(), gpu.graphicsQueue(), cmd);
}

// ---------------------------------------------------------------------------
// Swapchain recreation
// ---------------------------------------------------------------------------

void Renderer::recreateSwapchain(GpuDevice& gpu, Window& window) {
    // Wait while minimised — framebuffer size reports 0x0.
    int w = 0, h = 0;
    while (w == 0 || h == 0) {
        glfwGetFramebufferSize(window.getHandle(), &w, &h);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(gpu.device());

    // Destroy renderFinished semaphores before recreating the swapchain — image
    // count may change, so we resize the vector to match the new swapchain.
    for (auto& sem : renderFinished)
        vkDestroySemaphore(gpu.device(), sem, nullptr);
    renderFinished.clear();

    destroyFramebuffers(gpu.device());
    swapchain->recreate(gpu, window);
    createFramebuffers(gpu.device());

    VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    renderFinished.resize(swapchain->imageCount());
    for (auto& sem : renderFinished)
        if (vkCreateSemaphore(gpu.device(), &si, nullptr, &sem) != VK_SUCCESS)
            throw std::runtime_error("Failed to recreate renderFinished semaphores");
}

// ---------------------------------------------------------------------------
// Command buffer recording
// ---------------------------------------------------------------------------

void Renderer::recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &bi);

    VkClearValue clearColor{ .color = {0.05f, 0.05f, 0.05f, 1.0f} };

    VkRenderPassBeginInfo rpbi{};
    rpbi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass        = renderPass->get();
    rpbi.framebuffer       = framebuffers[imageIndex];
    rpbi.renderArea.offset = {0, 0};
    rpbi.renderArea.extent = swapchain->extent();
    rpbi.clearValueCount   = 1;
    rpbi.pClearValues      = &clearColor;

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

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
    vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);

    vkCmdEndRenderPass(cmd);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
        throw std::runtime_error("Failed to record command buffer");
}

// ---------------------------------------------------------------------------
// drawFrame
// ---------------------------------------------------------------------------
void Renderer::drawFrame(GpuDevice& gpu, Window& window) {
    vkWaitForFences(gpu.device(), 1, &inFlightFence[currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(gpu.device(), swapchain->handle(),
                                            UINT64_MAX, imageAvailable[currentFrame],
                                            VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain(gpu, window);
        return; // fence stays signaled — safe to wait on it again next frame
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("Failed to acquire swapchain image");

    // Only reset after confirming we will submit work this frame.
    vkResetFences(gpu.device(), 1, &inFlightFence[currentFrame]);

    vkResetCommandBuffer(cmdBuffers[currentFrame], 0);
    recordCommandBuffer(cmdBuffers[currentFrame], imageIndex);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = &imageAvailable[currentFrame];
    si.pWaitDstStageMask    = &waitStage;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &cmdBuffers[currentFrame];
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &renderFinished[imageIndex]; // per-image, not per-frame

    if (vkQueueSubmit(gpu.graphicsQueue(), 1, &si, inFlightFence[currentFrame]) != VK_SUCCESS)
        throw std::runtime_error("Failed to submit draw command buffer");

    VkSwapchainKHR sc = swapchain->handle();
    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &renderFinished[imageIndex]; // must match what submit signaled
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
