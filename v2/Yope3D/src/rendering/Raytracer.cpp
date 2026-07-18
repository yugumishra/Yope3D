#include "Raytracer.h"
#include "gpu/GpuDevice.h"
#include "gpu/Swapchain.h"
#include "gpu/ShaderModule.h"
#include "world/World.h"
#include "world/RenderMesh.h"
#include "../ecs/Components.h"
#include <stdexcept>
#include <array>
#include <variant>
#include <cstring>

// ---------------------------------------------------------------------------
// Geometry SSBO primitive tags (must match raytracer.comp)
// ---------------------------------------------------------------------------
static constexpr float GEO_SPHERE   = 0.0f;
static constexpr float GEO_QUAD     = 1.0f;
static constexpr float GEO_TRIANGLE = 2.0f;

// ---------------------------------------------------------------------------
// Helper: push a quad record [type=1, refl, r, g, b, origin.xyz, e1.xyz, e2.xyz]
// ---------------------------------------------------------------------------
static void pushQuad(std::vector<float>& out,
                     const math::Vec3& origin, const math::Vec3& e1, const math::Vec3& e2,
                     float refl, float r, float g, float b) {
    out.push_back(GEO_QUAD);
    out.push_back(refl);
    out.push_back(r); out.push_back(g); out.push_back(b);
    out.push_back(origin.x); out.push_back(origin.y); out.push_back(origin.z);
    out.push_back(e1.x); out.push_back(e1.y); out.push_back(e1.z);
    out.push_back(e2.x); out.push_back(e2.y); out.push_back(e2.z);
}

// ---------------------------------------------------------------------------
// Raytracer constructor
// ---------------------------------------------------------------------------
Raytracer::Raytracer(GpuDevice& gpu, VkCommandPool cmdPool,
                     const Swapchain& swapchain, VkRenderPass raytraceRenderPass,
                     std::array<VkBuffer, 2> uboBuffers, std::array<VkDeviceSize, 2> uboSizes,
                     std::array<VkBuffer, 2> lightBuffers, std::array<VkDeviceSize, 2> lightSizes)
    : gpu_(&gpu), swapchain_(&swapchain), raytracePass_(raytraceRenderPass),
      uboBuffers_(uboBuffers), uboSizes_(uboSizes),
      lightBuffers_(lightBuffers), lightSizes_(lightSizes) {

    // Geometry storage buffers
    VkDeviceSize geoSize = MAX_GEOMETRY_FLOATS * sizeof(float);
    for (auto& sb : geometryBuffers_)
        sb = StorageBuffer(gpu, geoSize);

    // Storage images (one per frame-in-flight, both transitioning to GENERAL)
    uint32_t w = swapchain.extent().width;
    uint32_t h = swapchain.extent().height;
    for (auto& si : storageImages_)
        si = StorageImage(gpu, w, h, VK_FORMAT_R8G8B8A8_UNORM, cmdPool);

    // Build compute pipeline + descriptors
    createComputeLayout(gpu.device());
    createComputePool(gpu.device());
    createComputePipeline(gpu.device(), cmdPool);
    writeComputeDescriptors(gpu.device(), 0);
    writeComputeDescriptors(gpu.device(), 1);

    // Build fullscreen blit pipeline + descriptors
    createBlitLayout(gpu.device());
    createBlitPool(gpu.device());
    createBlitPipeline(gpu);
    writeBlitDescriptors(gpu.device(), 0);
    writeBlitDescriptors(gpu.device(), 1);

    createFramebuffers(gpu.device());
}

Raytracer::~Raytracer() {
    if (!gpu_) return;
    VkDevice dev = gpu_->device();

    destroyFramebuffers(dev);

    if (blitPipeline_    != VK_NULL_HANDLE) vkDestroyPipeline(dev, blitPipeline_, nullptr);
    if (blitPipeLayout_  != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, blitPipeLayout_, nullptr);
    if (blitPool_        != VK_NULL_HANDLE) vkDestroyDescriptorPool(dev, blitPool_, nullptr);
    if (blitSetLayout_   != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, blitSetLayout_, nullptr);

    computePipeline_ = ComputePipeline{};
    if (computePipeLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, computePipeLayout_, nullptr);
    if (computePool_       != VK_NULL_HANDLE) vkDestroyDescriptorPool(dev, computePool_, nullptr);
    if (computeSetLayout_  != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, computeSetLayout_, nullptr);

    for (auto& sb : geometryBuffers_) sb.destroy(dev);
    for (auto& si : storageImages_)   si.destroy(dev);
}

// ---------------------------------------------------------------------------
// Resize — recreate size-dependent resources (storage images + framebuffers)
// ---------------------------------------------------------------------------
void Raytracer::onResize(GpuDevice& gpu, uint32_t newWidth, uint32_t newHeight, VkCommandPool cmdPool) {
    destroyFramebuffers(gpu.device());
    for (auto& si : storageImages_) {
        si.destroy(gpu.device());
        si = StorageImage(gpu, newWidth, newHeight, VK_FORMAT_R8G8B8A8_UNORM, cmdPool);
    }
    writeComputeDescriptors(gpu.device(), 0);
    writeComputeDescriptors(gpu.device(), 1);
    writeBlitDescriptors(gpu.device(), 0);
    writeBlitDescriptors(gpu.device(), 1);
    createFramebuffers(gpu.device());
}

// ---------------------------------------------------------------------------
// prepareFrame — pack world geometry into the geometry SSBO for this frame
// ---------------------------------------------------------------------------
void Raytracer::prepareFrame(uint32_t frameIndex, World& world) {
    std::vector<float> packed;
    packed.reserve(1024);
    packed.push_back(0.0f);

    packGeometry(world, packed);

    // Clamp to buffer
    if (packed.size() > MAX_GEOMETRY_FLOATS) {
        packed.resize(MAX_GEOMETRY_FLOATS);
    }

    geometryBuffers_[frameIndex].write(packed.data(), packed.size() * sizeof(float));
}

// ---------------------------------------------------------------------------
// dispatch — compute pass (called outside any render pass)
// ---------------------------------------------------------------------------
void Raytracer::dispatch(VkCommandBuffer cmd, uint32_t frameIndex) {
    StorageImage& si = storageImages_[frameIndex];

    // Transition: GENERAL (or whatever previous frame left) → GENERAL for write.
    // Use UNDEFINED as source to discard previous contents — we rewrite every pixel.
    si.transition(cmd,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, VK_ACCESS_SHADER_WRITE_BIT);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline_.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeLayout_,
        0, 1, &computeSets_[frameIndex], 0, nullptr);

    uint32_t w = swapchain_->extent().width;
    uint32_t h = swapchain_->extent().height;
    vkCmdDispatch(cmd, (w + 7) / 8, (h + 3) / 4, 1);

    // Barrier: compute write → fragment shader read
    si.transition(cmd,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
}

// ---------------------------------------------------------------------------
// recordBlit — fullscreen triangle (called inside raytrace render pass)
// ---------------------------------------------------------------------------
void Raytracer::recordBlit(VkCommandBuffer cmd, uint32_t frameIndex) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blitPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blitPipeLayout_,
        0, 1, &blitSets_[frameIndex], 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

// ---------------------------------------------------------------------------
// Internal setup helpers
// ---------------------------------------------------------------------------
void Raytracer::createComputeLayout(VkDevice device) {
    VkDescriptorSetLayoutBinding bindings[4]{};
    // 0: GlobalUBO
    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    // 1: LightBuffer SSBO
    bindings[1].binding         = 1;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    // 2: GeometryBuffer SSBO
    bindings[2].binding         = 2;
    bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    // 3: StorageImage output
    bindings[3].binding         = 3;
    bindings[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = 4;
    ci.pBindings    = bindings;
    if (vkCreateDescriptorSetLayout(device, &ci, nullptr, &computeSetLayout_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create compute descriptor set layout");
}

void Raytracer::createComputePool(VkDevice device) {
    VkDescriptorPoolSize sizes[3]{};
    sizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sizes[0].descriptorCount = MAX_FRAMES;
    sizes[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    sizes[1].descriptorCount = MAX_FRAMES * 2;  // light + geometry
    sizes[2].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    sizes[2].descriptorCount = MAX_FRAMES;

    VkDescriptorPoolCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.poolSizeCount = 3;
    ci.pPoolSizes    = sizes;
    ci.maxSets       = MAX_FRAMES;
    if (vkCreateDescriptorPool(device, &ci, nullptr, &computePool_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create compute descriptor pool");

    // Allocate sets immediately
    VkDescriptorSetLayout layouts[MAX_FRAMES] = { computeSetLayout_, computeSetLayout_ };
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = computePool_;
    ai.descriptorSetCount = MAX_FRAMES;
    ai.pSetLayouts        = layouts;
    if (vkAllocateDescriptorSets(device, &ai, computeSets_.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate compute descriptor sets");
}

void Raytracer::createComputePipeline(VkDevice device, VkCommandPool /*cmdPool*/) {
    VkPipelineLayoutCreateInfo li{};
    li.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    li.setLayoutCount = 1;
    li.pSetLayouts    = &computeSetLayout_;
    if (vkCreatePipelineLayout(device, &li, nullptr, &computePipeLayout_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create compute pipeline layout");

    ShaderModule cs(device, std::string(YOPE_SHADER_DIR) + "/raytracer.comp.spv");
    computePipeline_ = ComputePipeline(device, computePipeLayout_, cs.get());
}

void Raytracer::createBlitLayout(VkDevice device) {
    VkDescriptorSetLayoutBinding b{};
    b.binding         = 0;
    b.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = 1;
    ci.pBindings    = &b;
    if (vkCreateDescriptorSetLayout(device, &ci, nullptr, &blitSetLayout_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create blit descriptor set layout");
}

void Raytracer::createBlitPool(VkDevice device) {
    VkDescriptorPoolSize size{};
    size.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    size.descriptorCount = MAX_FRAMES;

    VkDescriptorPoolCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.poolSizeCount = 1;
    ci.pPoolSizes    = &size;
    ci.maxSets       = MAX_FRAMES;
    if (vkCreateDescriptorPool(device, &ci, nullptr, &blitPool_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create blit descriptor pool");

    VkDescriptorSetLayout layouts[MAX_FRAMES] = { blitSetLayout_, blitSetLayout_ };
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = blitPool_;
    ai.descriptorSetCount = MAX_FRAMES;
    ai.pSetLayouts        = layouts;
    if (vkAllocateDescriptorSets(device, &ai, blitSets_.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate blit descriptor sets");
}

void Raytracer::createBlitPipeline(GpuDevice& gpu) {
    VkPipelineLayoutCreateInfo li{};
    li.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    li.setLayoutCount = 1;
    li.pSetLayouts    = &blitSetLayout_;
    if (vkCreatePipelineLayout(gpu.device(), &li, nullptr, &blitPipeLayout_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create blit pipeline layout");

    ShaderModule vert(gpu.device(), std::string(YOPE_SHADER_DIR) + "/fullscreen.vert.spv");
    ShaderModule frag(gpu.device(), std::string(YOPE_SHADER_DIR) + "/fullscreen.frag.spv");

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert.get();
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag.get();
    stages[1].pName  = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    // No vertex buffers — gl_VertexIndex trick

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynState{};
    dynState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates    = dynStates;

    VkPipelineViewportStateCreateInfo vs{};
    vs.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vs.viewportCount = 1;
    vs.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType           = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_FALSE;
    ds.depthWriteEnable= VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAtt{};
    blendAtt.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments    = &blendAtt;

    VkGraphicsPipelineCreateInfo pi{};
    pi.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount          = 2;
    pi.pStages             = stages;
    pi.pVertexInputState   = &vertexInput;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState      = &vs;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState   = &ms;
    pi.pColorBlendState    = &cb;
    pi.pDepthStencilState  = &ds;
    pi.pDynamicState       = &dynState;
    pi.layout              = blitPipeLayout_;
    pi.renderPass          = raytracePass_;
    pi.subpass             = 0;

    if (vkCreateGraphicsPipelines(gpu.device(), VK_NULL_HANDLE, 1, &pi, nullptr, &blitPipeline_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create blit graphics pipeline");
}

void Raytracer::createFramebuffers(VkDevice device) {
    const auto& views = swapchain_->imageViews();
    framebuffers.resize(views.size());
    for (size_t i = 0; i < views.size(); ++i) {
        VkFramebufferCreateInfo fi{};
        fi.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass      = raytracePass_;
        fi.attachmentCount = 1;
        fi.pAttachments    = &views[i];
        fi.width           = swapchain_->extent().width;
        fi.height          = swapchain_->extent().height;
        fi.layers          = 1;
        if (vkCreateFramebuffer(device, &fi, nullptr, &framebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create raytrace framebuffer");
    }
}

void Raytracer::destroyFramebuffers(VkDevice device) {
    for (auto fb : framebuffers) vkDestroyFramebuffer(device, fb, nullptr);
    framebuffers.clear();
}

void Raytracer::writeComputeDescriptors(VkDevice device, uint32_t frameIndex) {
    VkDescriptorBufferInfo uboBuf{};
    uboBuf.buffer = uboBuffers_[frameIndex];
    uboBuf.offset = 0;
    uboBuf.range  = uboSizes_[frameIndex];

    VkDescriptorBufferInfo lightBuf{};
    lightBuf.buffer = lightBuffers_[frameIndex];
    lightBuf.offset = 0;
    lightBuf.range  = lightSizes_[frameIndex];

    VkDescriptorBufferInfo geoBuf{};
    geoBuf.buffer = geometryBuffers_[frameIndex].get();
    geoBuf.offset = 0;
    geoBuf.range  = geometryBuffers_[frameIndex].getSize();

    VkDescriptorImageInfo imgInfo{};
    imgInfo.imageView   = storageImages_[frameIndex].view();
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imgInfo.sampler     = VK_NULL_HANDLE;  // storage images don't need a sampler

    VkWriteDescriptorSet writes[4]{};
    writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    writes[0].dstSet          = computeSets_[frameIndex];
    writes[0].dstBinding      = 0;
    writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo     = &uboBuf;

    writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    writes[1].dstSet          = computeSets_[frameIndex];
    writes[1].dstBinding      = 1;
    writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    writes[1].pBufferInfo     = &lightBuf;

    writes[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    writes[2].dstSet          = computeSets_[frameIndex];
    writes[2].dstBinding      = 2;
    writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    writes[2].pBufferInfo     = &geoBuf;

    writes[3] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    writes[3].dstSet          = computeSets_[frameIndex];
    writes[3].dstBinding      = 3;
    writes[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[3].descriptorCount = 1;
    writes[3].pImageInfo      = &imgInfo;

    vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
}

void Raytracer::writeBlitDescriptors(VkDevice device, uint32_t frameIndex) {
    VkDescriptorImageInfo imgInfo{};
    imgInfo.imageView   = storageImages_[frameIndex].view();
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imgInfo.sampler     = storageImages_[frameIndex].sampler();

    VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    w.dstSet          = blitSets_[frameIndex];
    w.dstBinding      = 0;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.descriptorCount = 1;
    w.pImageInfo      = &imgInfo;

    vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
}

// ---------------------------------------------------------------------------
// packGeometry — traverse the World and pack primitives into the float buffer.
// Iterates ecs::MeshRenderer components; reads mr.mesh->modelMatrix (snapshot).
// ---------------------------------------------------------------------------
void Raytracer::packGeometry(World& world, std::vector<float>& out) {
    int count = 0;

    // Helper: build 6 oriented box quads from a center, three axis vectors, and half-extents.
    auto pushBox = [&](const math::Vec3& p,
                       const math::Vec3& ax, const math::Vec3& ay, const math::Vec3& az,
                       const math::Vec3& e, float refl, float r, float g, float b) {
        struct Face { math::Vec3 fc, e1, e2; };
        const Face faces[6] = {
            { p + ax*e.x,  ay*(2*e.y),   az*(2*e.z)  },
            { p - ax*e.x,  az*(2*e.z),   ay*(2*e.y)  },
            { p + ay*e.y,  ax*(2*e.x),   az*(-2*e.z) },
            { p - ay*e.y,  ax*(2*e.x),   az*(2*e.z)  },
            { p + az*e.z,  ax*(2*e.x),   ay*(2*e.y)  },
            { p - az*e.z,  ay*(2*e.y),   ax*(2*e.x)  },
        };
        for (const auto& f : faces) {
            pushQuad(out, f.fc - f.e1*0.5f - f.e2*0.5f, f.e1, f.e2, refl, r, g, b);
            ++count;
        }
    };

    for (auto [entity, mr] : world.getRegistry().view<ecs::MeshRenderer>()) {
        const RenderMesh* mesh = mr.mesh;
        if (!mesh || !mesh->transformReady) continue;
        if (out.size() + 84 > MAX_GEOMETRY_FLOATS) break;

        const float r    = mesh->color[0];
        const float g    = mesh->color[1];
        const float b    = mesh->color[2];
        const float refl = mesh->reflectivity;

        // Extract world position (column 3) and axis columns from column-major Mat4.
        // Column-major layout: m[col*4 + row]. Translation = col 3 = m[12..14].
        const auto& mm = mesh->modelMatrix.m;
        const math::Vec3 p  = { mm[12], mm[13], mm[14] };
        const math::Vec3 ax = { mm[0],  mm[1],  mm[2]  };  // col 0
        const math::Vec3 ay = { mm[4],  mm[5],  mm[6]  };  // col 1
        const math::Vec3 az = { mm[8],  mm[9],  mm[10] };  // col 2

        switch (mesh->primitiveType) {

            case PrimitiveType::Sphere:
            case PrimitiveType::Icosphere: {
                // World-space radius = baked mesh radius * uniform scale.
                // Scale is encoded in the model matrix column lengths (ax = col0).
                float worldScale = std::sqrt(ax.x*ax.x + ax.y*ax.y + ax.z*ax.z);
                out.push_back(GEO_SPHERE); out.push_back(refl);
                out.push_back(r); out.push_back(g); out.push_back(b);
                out.push_back(p.x); out.push_back(p.y); out.push_back(p.z);
                out.push_back(mesh->primitiveExtents.x * worldScale);
                ++count;
                break;
            }

            case PrimitiveType::Rect:
            case PrimitiveType::Cube: {
                pushBox(p, ax, ay, az, mesh->primitiveExtents, refl, r, g, b);
                break;
            }

            case PrimitiveType::Plane: {
                // Plane vertices live at local y=-1. Subtract 1 along the local Y axis
                // (column 1 of modelMatrix) to arrive at the actual world surface center.
                const float he = mesh->primitiveExtents.x;
                const math::Vec3 center = p - ay;
                pushQuad(out, center + ax*(-he) + az*(-he), ax*(2*he), az*(2*he), refl, r, g, b);
                ++count;
                break;
            }

            case PrimitiveType::Custom:
            default: {
                for (size_t t = 0; t + 2 < mesh->cpuIndices.size(); t += 3) {
                    if (out.size() + 14 > MAX_GEOMETRY_FLOATS) break;
                    auto xform = [&](uint32_t idx) {
                        const auto& v  = mesh->cpuVertices[idx];
                        const auto& mm = mesh->modelMatrix.m;
                        return math::Vec3{
                            mm[0]*v.position[0] + mm[4]*v.position[1] + mm[8] *v.position[2] + mm[12],
                            mm[1]*v.position[0] + mm[5]*v.position[1] + mm[9] *v.position[2] + mm[13],
                            mm[2]*v.position[0] + mm[6]*v.position[1] + mm[10]*v.position[2] + mm[14]
                        };
                    };
                    const math::Vec3 v0 = xform(mesh->cpuIndices[t]);
                    const math::Vec3 v1 = xform(mesh->cpuIndices[t+1]);
                    const math::Vec3 v2 = xform(mesh->cpuIndices[t+2]);
                    out.push_back(GEO_TRIANGLE); out.push_back(refl);
                    out.push_back(r); out.push_back(g); out.push_back(b);
                    out.push_back(v0.x); out.push_back(v0.y); out.push_back(v0.z);
                    out.push_back(v1.x); out.push_back(v1.y); out.push_back(v1.z);
                    out.push_back(v2.x); out.push_back(v2.y); out.push_back(v2.z);
                    ++count;
                }
                break;
            }
        }
    }

    out[0] = static_cast<float>(count);
}
