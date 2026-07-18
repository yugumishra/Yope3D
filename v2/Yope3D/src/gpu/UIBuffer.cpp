#include "UIBuffer.h"
#include "GpuDevice.h"
#include <cstring>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

uint32_t UIBuffer::findMemoryType(VkPhysicalDevice pd, uint32_t filter,
                                   VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(pd, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        if ((filter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("UIBuffer: failed to find suitable memory type");
}

VkBuffer UIBuffer::makeBuffer(VkDevice device, VkPhysicalDevice pd,
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
        throw std::runtime_error("UIBuffer: failed to create buffer");

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device, buf, &req);

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = findMemoryType(pd, req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(device, &ai, nullptr, &outMem) != VK_SUCCESS) {
        vkDestroyBuffer(device, buf, nullptr);
        throw std::runtime_error("UIBuffer: failed to allocate buffer memory");
    }
    vkBindBufferMemory(device, buf, outMem, 0);
    return buf;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void UIBuffer::init(GpuDevice& gpu) {
    VkDevice         dev = gpu.device();
    VkPhysicalDevice pd  = gpu.physicalDevice();

    VkDeviceSize vertSize = sizeof(UIVertex) * kMaxVertices;
    VkDeviceSize idxSize  = sizeof(uint32_t) * kMaxIndices;

    vertBuf_ = makeBuffer(dev, pd, vertSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertMem_);
    idxBuf_  = makeBuffer(dev, pd, idxSize,  VK_BUFFER_USAGE_INDEX_BUFFER_BIT,  idxMem_);

    void* vp; void* ip;
    vkMapMemory(dev, vertMem_, 0, vertSize, 0, &vp);
    vkMapMemory(dev, idxMem_,  0, idxSize,  0, &ip);
    mappedVerts_ = static_cast<UIVertex*>(vp);
    mappedIdx_   = static_cast<uint32_t*>(ip);
}

void UIBuffer::destroy(VkDevice device) {
    if (mappedVerts_) { vkUnmapMemory(device, vertMem_); mappedVerts_ = nullptr; }
    if (mappedIdx_)   { vkUnmapMemory(device, idxMem_);  mappedIdx_   = nullptr; }
    if (vertBuf_ != VK_NULL_HANDLE) { vkDestroyBuffer(device, vertBuf_, nullptr); vertBuf_ = VK_NULL_HANDLE; }
    if (vertMem_ != VK_NULL_HANDLE) { vkFreeMemory(device, vertMem_, nullptr);    vertMem_ = VK_NULL_HANDLE; }
    if (idxBuf_  != VK_NULL_HANDLE) { vkDestroyBuffer(device, idxBuf_,  nullptr); idxBuf_  = VK_NULL_HANDLE; }
    if (idxMem_  != VK_NULL_HANDLE) { vkFreeMemory(device, idxMem_,  nullptr);    idxMem_  = VK_NULL_HANDLE; }
}

// ---------------------------------------------------------------------------
// Move
// ---------------------------------------------------------------------------

UIBuffer::UIBuffer(UIBuffer&& o) noexcept
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

UIBuffer& UIBuffer::operator=(UIBuffer&& o) noexcept {
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

// ---------------------------------------------------------------------------
// Frame operations
// ---------------------------------------------------------------------------

void UIBuffer::begin() {
    vertHead_ = 0;
    idxHead_  = 0;
}

UIBuffer::Range UIBuffer::push(const UIVertex* verts, uint32_t vertCount,
                               const uint32_t* indices, uint32_t idxCount)
{
    if (vertHead_ + vertCount > kMaxVertices || idxHead_ + idxCount > kMaxIndices) {
        // Silently drop if over capacity — not expected in normal use.
        return { idxHead_, 0, static_cast<int32_t>(vertHead_) };
    }

    std::memcpy(mappedVerts_ + vertHead_, verts, vertCount * sizeof(UIVertex));
    std::memcpy(mappedIdx_   + idxHead_,  indices, idxCount * sizeof(uint32_t));

    Range r{ idxHead_, idxCount, static_cast<int32_t>(vertHead_) };
    vertHead_ += vertCount;
    idxHead_  += idxCount;
    return r;
}
