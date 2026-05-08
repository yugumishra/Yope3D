#include "UniformBuffer.h"
#include "GpuDevice.h"
#include <cstring>
#include <stdexcept>

UniformBuffer::UniformBuffer(GpuDevice& gpu, VkDeviceSize size) : bufSize(size) {
    VkBufferCreateInfo ci{};
    ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size        = size;
    ci.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(gpu.device(), &ci, nullptr, &buffer) != VK_SUCCESS)
        throw std::runtime_error("Failed to create uniform buffer");

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(gpu.device(), buffer, &req);

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = findMemoryType(gpu.physicalDevice(), req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(gpu.device(), &ai, nullptr, &memory) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate uniform buffer memory");

    vkBindBufferMemory(gpu.device(), buffer, memory, 0);
    vkMapMemory(gpu.device(), memory, 0, size, 0, &mapped);
}

UniformBuffer::UniformBuffer(UniformBuffer&& o) noexcept
    : buffer(o.buffer), memory(o.memory), bufSize(o.bufSize), mapped(o.mapped)
{
    o.buffer = VK_NULL_HANDLE; o.memory = VK_NULL_HANDLE;
    o.bufSize = 0;             o.mapped = nullptr;
}

UniformBuffer& UniformBuffer::operator=(UniformBuffer&& o) noexcept {
    buffer  = o.buffer;  memory  = o.memory;
    bufSize = o.bufSize; mapped  = o.mapped;
    o.buffer = VK_NULL_HANDLE; o.memory = VK_NULL_HANDLE;
    o.bufSize = 0;             o.mapped = nullptr;
    return *this;
}

void UniformBuffer::destroy(VkDevice device) {
    if (mapped != nullptr)         { vkUnmapMemory(device, memory);          mapped  = nullptr;        }
    if (buffer != VK_NULL_HANDLE)  { vkDestroyBuffer(device, buffer, nullptr); buffer = VK_NULL_HANDLE; }
    if (memory != VK_NULL_HANDLE)  { vkFreeMemory(device, memory, nullptr);   memory = VK_NULL_HANDLE; }
    bufSize = 0;
}

void UniformBuffer::write(const void* data, VkDeviceSize size) {
    memcpy(mapped, data, static_cast<size_t>(size));
}

uint32_t UniformBuffer::findMemoryType(VkPhysicalDevice pd, uint32_t filter,
                                       VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(pd, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        if ((filter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("Failed to find suitable memory type for uniform buffer");
}
