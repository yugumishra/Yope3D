#include "DynamicMeshBuffer.h"
#include "GpuDevice.h"
#include <cstring>
#include <stdexcept>
#include <algorithm>

uint32_t DynamicMeshBuffer::findMemoryType(VkPhysicalDevice pd, uint32_t filter,
                                           VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(pd, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        if ((filter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("DynamicMeshBuffer: failed to find suitable memory type");
}

VkBuffer DynamicMeshBuffer::makeBuffer(VkDevice device, VkPhysicalDevice pd,
                                       VkDeviceSize size, VkBufferUsageFlags usage,
                                       VkDeviceMemory& outMem)
{
    VkBufferCreateInfo ci{};
    ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size        = size;
    ci.usage       = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buf;
    if (vkCreateBuffer(device, &ci, nullptr, &buf) != VK_SUCCESS)
        throw std::runtime_error("DynamicMeshBuffer: failed to create buffer");

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device, buf, &req);

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = findMemoryType(pd, req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(device, &ai, nullptr, &outMem) != VK_SUCCESS) {
        vkDestroyBuffer(device, buf, nullptr);
        throw std::runtime_error("DynamicMeshBuffer: failed to allocate buffer memory");
    }
    vkBindBufferMemory(device, buf, outMem, 0);
    return buf;
}

void DynamicMeshBuffer::init(GpuDevice& gpu, uint32_t maxVertices,
                             uint32_t vertexStride, uint32_t maxIndices)
{
    VkDevice         dev = gpu.device();
    VkPhysicalDevice pd  = gpu.physicalDevice();

    maxVerts_     = std::max(1u, maxVertices);
    maxIndices_   = std::max(1u, maxIndices);
    vertexStride_ = vertexStride;

    VkDeviceSize vertSize = static_cast<VkDeviceSize>(vertexStride_) * maxVerts_;
    VkDeviceSize idxSize  = sizeof(uint32_t) * static_cast<VkDeviceSize>(maxIndices_);

    // STORAGE on top of VERTEX mirrors RenderMesh's static path, where the flag is
    // set unconditionally so skin.comp can read any vertex buffer as an SSBO. A
    // dynamic mesh is never skinned today, but a usage flag costs nothing and
    // keeps the two buffers interchangeable at the binding site.
    vertBuf_ = makeBuffer(dev, pd, vertSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, vertMem_);
    idxBuf_  = makeBuffer(dev, pd, idxSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, idxMem_);

    void* vp; void* ip;
    vkMapMemory(dev, vertMem_, 0, vertSize, 0, &vp);
    vkMapMemory(dev, idxMem_,  0, idxSize,  0, &ip);
    mappedVerts_ = static_cast<uint8_t*>(vp);
    mappedIdx_   = static_cast<uint32_t*>(ip);
}

void DynamicMeshBuffer::destroy(VkDevice device) {
    if (mappedVerts_) { vkUnmapMemory(device, vertMem_); mappedVerts_ = nullptr; }
    if (mappedIdx_)   { vkUnmapMemory(device, idxMem_);  mappedIdx_   = nullptr; }
    if (vertBuf_ != VK_NULL_HANDLE) { vkDestroyBuffer(device, vertBuf_, nullptr); vertBuf_ = VK_NULL_HANDLE; }
    if (vertMem_ != VK_NULL_HANDLE) { vkFreeMemory(device, vertMem_, nullptr);    vertMem_ = VK_NULL_HANDLE; }
    if (idxBuf_  != VK_NULL_HANDLE) { vkDestroyBuffer(device, idxBuf_,  nullptr); idxBuf_  = VK_NULL_HANDLE; }
    if (idxMem_  != VK_NULL_HANDLE) { vkFreeMemory(device, idxMem_,  nullptr);    idxMem_  = VK_NULL_HANDLE; }
    idxCount_ = 0;
}

void DynamicMeshBuffer::write(const void* verts, uint32_t vertCount,
                              const uint32_t* indices, uint32_t idxCount)
{
    if (!mappedVerts_ || !mappedIdx_) return;

    const uint32_t nv = std::min(vertCount, maxVerts_);
    const uint32_t ni = std::min(idxCount,  maxIndices_);

    if (nv && verts)
        std::memcpy(mappedVerts_, verts, static_cast<size_t>(nv) * vertexStride_);
    if (ni && indices)
        std::memcpy(mappedIdx_, indices, static_cast<size_t>(ni) * sizeof(uint32_t));

    idxCount_ = ni;
}

// ---------------------------------------------------------------------------
// Move
// ---------------------------------------------------------------------------

DynamicMeshBuffer::DynamicMeshBuffer(DynamicMeshBuffer&& o) noexcept
    : vertBuf_(o.vertBuf_), vertMem_(o.vertMem_),
      idxBuf_(o.idxBuf_),   idxMem_(o.idxMem_),
      mappedVerts_(o.mappedVerts_), mappedIdx_(o.mappedIdx_),
      maxVerts_(o.maxVerts_), maxIndices_(o.maxIndices_),
      vertexStride_(o.vertexStride_), idxCount_(o.idxCount_)
{
    o.vertBuf_ = VK_NULL_HANDLE; o.vertMem_ = VK_NULL_HANDLE;
    o.idxBuf_  = VK_NULL_HANDLE; o.idxMem_  = VK_NULL_HANDLE;
    o.mappedVerts_ = nullptr; o.mappedIdx_ = nullptr;
    o.maxVerts_ = 0; o.maxIndices_ = 0; o.vertexStride_ = 0; o.idxCount_ = 0;
}

DynamicMeshBuffer& DynamicMeshBuffer::operator=(DynamicMeshBuffer&& o) noexcept {
    vertBuf_ = o.vertBuf_; vertMem_ = o.vertMem_;
    idxBuf_  = o.idxBuf_;  idxMem_  = o.idxMem_;
    mappedVerts_ = o.mappedVerts_; mappedIdx_ = o.mappedIdx_;
    maxVerts_ = o.maxVerts_; maxIndices_ = o.maxIndices_;
    vertexStride_ = o.vertexStride_; idxCount_ = o.idxCount_;
    o.vertBuf_ = VK_NULL_HANDLE; o.vertMem_ = VK_NULL_HANDLE;
    o.idxBuf_  = VK_NULL_HANDLE; o.idxMem_  = VK_NULL_HANDLE;
    o.mappedVerts_ = nullptr; o.mappedIdx_ = nullptr;
    o.maxVerts_ = 0; o.maxIndices_ = 0; o.vertexStride_ = 0; o.idxCount_ = 0;
    return *this;
}
