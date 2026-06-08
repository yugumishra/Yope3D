#include "LineBuffer.h"
#include "GpuDevice.h"
#include <cstring>
#include <stdexcept>
#include <algorithm>

uint32_t LineBuffer::findMemoryType(VkPhysicalDevice pd, uint32_t filter,
                                    VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(pd, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        if ((filter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("LineBuffer: failed to find suitable memory type");
}

VkBuffer LineBuffer::makeBuffer(VkDevice device, VkPhysicalDevice pd,
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
        throw std::runtime_error("LineBuffer: failed to create buffer");

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device, buf, &req);

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = findMemoryType(pd, req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(device, &ai, nullptr, &outMem) != VK_SUCCESS) {
        vkDestroyBuffer(device, buf, nullptr);
        throw std::runtime_error("LineBuffer: failed to allocate buffer memory");
    }
    vkBindBufferMemory(device, buf, outMem, 0);
    return buf;
}

void LineBuffer::init(GpuDevice& gpu) {
    VkDevice         dev = gpu.device();
    VkPhysicalDevice pd  = gpu.physicalDevice();

    VkDeviceSize vertSize = sizeof(DebugLineVertex) * kMaxVertices;
    vertBuf_ = makeBuffer(dev, pd, vertSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertMem_);

    void* vp;
    vkMapMemory(dev, vertMem_, 0, vertSize, 0, &vp);
    mapped_ = static_cast<DebugLineVertex*>(vp);
}

void LineBuffer::destroy(VkDevice device) {
    if (mapped_)  { vkUnmapMemory(device, vertMem_); mapped_ = nullptr; }
    if (vertBuf_ != VK_NULL_HANDLE) { vkDestroyBuffer(device, vertBuf_, nullptr); vertBuf_ = VK_NULL_HANDLE; }
    if (vertMem_ != VK_NULL_HANDLE) { vkFreeMemory(device, vertMem_, nullptr);    vertMem_ = VK_NULL_HANDLE; }
}

LineBuffer::LineBuffer(LineBuffer&& o) noexcept
    : vertBuf_(o.vertBuf_), vertMem_(o.vertMem_), mapped_(o.mapped_)
{
    o.vertBuf_ = VK_NULL_HANDLE; o.vertMem_ = VK_NULL_HANDLE; o.mapped_ = nullptr;
}

LineBuffer& LineBuffer::operator=(LineBuffer&& o) noexcept {
    vertBuf_ = o.vertBuf_; vertMem_ = o.vertMem_; mapped_ = o.mapped_;
    o.vertBuf_ = VK_NULL_HANDLE; o.vertMem_ = VK_NULL_HANDLE; o.mapped_ = nullptr;
    return *this;
}

uint32_t LineBuffer::upload(const DebugLineVertex* verts, uint32_t count) {
    if (!mapped_ || count == 0) return 0;
    uint32_t n = std::min(count, kMaxVertices);
    std::memcpy(mapped_, verts, n * sizeof(DebugLineVertex));
    return n;
}
