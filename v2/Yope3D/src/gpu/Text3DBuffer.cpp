#include "Text3DBuffer.h"
#include "GpuDevice.h"
#include <cstring>
#include <stdexcept>

uint32_t Text3DBuffer::findMemoryType(VkPhysicalDevice pd, uint32_t filter,
                                      VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(pd, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        if ((filter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("Text3DBuffer: failed to find suitable memory type");
}

VkBuffer Text3DBuffer::makeBuffer(VkDevice device, VkPhysicalDevice pd,
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
        throw std::runtime_error("Text3DBuffer: failed to create buffer");

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device, buf, &req);

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = findMemoryType(pd, req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(device, &ai, nullptr, &outMem) != VK_SUCCESS) {
        vkDestroyBuffer(device, buf, nullptr);
        throw std::runtime_error("Text3DBuffer: failed to allocate buffer memory");
    }
    vkBindBufferMemory(device, buf, outMem, 0);
    return buf;
}

void Text3DBuffer::init(GpuDevice& gpu) {
    VkDevice         dev = gpu.device();
    VkPhysicalDevice pd  = gpu.physicalDevice();

    VkDeviceSize vertSize = sizeof(Text3DVertex) * kMaxVertices;
    VkDeviceSize idxSize  = sizeof(uint32_t) * kMaxIndices;

    vertBuf_ = makeBuffer(dev, pd, vertSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertMem_);
    idxBuf_  = makeBuffer(dev, pd, idxSize,  VK_BUFFER_USAGE_INDEX_BUFFER_BIT,  idxMem_);

    void* vp; void* ip;
    vkMapMemory(dev, vertMem_, 0, vertSize, 0, &vp);
    vkMapMemory(dev, idxMem_,  0, idxSize,  0, &ip);
    mappedVerts_ = static_cast<Text3DVertex*>(vp);
    mappedIdx_   = static_cast<uint32_t*>(ip);
}

void Text3DBuffer::destroy(VkDevice device) {
    if (mappedVerts_) { vkUnmapMemory(device, vertMem_); mappedVerts_ = nullptr; }
    if (mappedIdx_)   { vkUnmapMemory(device, idxMem_);  mappedIdx_   = nullptr; }
    if (vertBuf_ != VK_NULL_HANDLE) { vkDestroyBuffer(device, vertBuf_, nullptr); vertBuf_ = VK_NULL_HANDLE; }
    if (vertMem_ != VK_NULL_HANDLE) { vkFreeMemory(device, vertMem_, nullptr);    vertMem_ = VK_NULL_HANDLE; }
    if (idxBuf_  != VK_NULL_HANDLE) { vkDestroyBuffer(device, idxBuf_,  nullptr); idxBuf_  = VK_NULL_HANDLE; }
    if (idxMem_  != VK_NULL_HANDLE) { vkFreeMemory(device, idxMem_,  nullptr);    idxMem_  = VK_NULL_HANDLE; }
}

Text3DBuffer::Text3DBuffer(Text3DBuffer&& o) noexcept
    : vertBuf_(o.vertBuf_), vertMem_(o.vertMem_),
      idxBuf_(o.idxBuf_),   idxMem_(o.idxMem_),
      mappedVerts_(o.mappedVerts_), mappedIdx_(o.mappedIdx_),
      vertHead_(o.vertHead_), idxHead_(o.idxHead_)
{
    o.vertBuf_ = VK_NULL_HANDLE; o.vertMem_ = VK_NULL_HANDLE;
    o.idxBuf_  = VK_NULL_HANDLE; o.idxMem_  = VK_NULL_HANDLE;
    o.mappedVerts_ = nullptr; o.mappedIdx_ = nullptr;
    o.vertHead_ = 0; o.idxHead_ = 0;
}

Text3DBuffer& Text3DBuffer::operator=(Text3DBuffer&& o) noexcept {
    vertBuf_ = o.vertBuf_; vertMem_ = o.vertMem_;
    idxBuf_  = o.idxBuf_;  idxMem_  = o.idxMem_;
    mappedVerts_ = o.mappedVerts_; mappedIdx_ = o.mappedIdx_;
    vertHead_ = o.vertHead_; idxHead_ = o.idxHead_;
    o.vertBuf_ = VK_NULL_HANDLE; o.vertMem_ = VK_NULL_HANDLE;
    o.idxBuf_  = VK_NULL_HANDLE; o.idxMem_  = VK_NULL_HANDLE;
    o.mappedVerts_ = nullptr; o.mappedIdx_ = nullptr;
    o.vertHead_ = 0; o.idxHead_ = 0;
    return *this;
}

void Text3DBuffer::begin() {
    vertHead_ = 0;
    idxHead_  = 0;
}

Text3DBuffer::Range Text3DBuffer::push(const Text3DVertex* verts, uint32_t vertCount,
                                       const uint32_t* indices, uint32_t idxCount)
{
    if (vertHead_ + vertCount > kMaxVertices || idxHead_ + idxCount > kMaxIndices) {
        return { idxHead_, 0, static_cast<int32_t>(vertHead_) };
    }

    std::memcpy(mappedVerts_ + vertHead_, verts, vertCount * sizeof(Text3DVertex));
    std::memcpy(mappedIdx_   + idxHead_,  indices, idxCount * sizeof(uint32_t));

    Range r{ idxHead_, idxCount, static_cast<int32_t>(vertHead_) };
    vertHead_ += vertCount;
    idxHead_  += idxCount;
    return r;
}
