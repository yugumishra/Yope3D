#include "Buffer.h"
#include "GpuDevice.h"
#include <cstring>
#include <stdexcept>

Buffer Buffer::uploadStaged(GpuDevice& gpu, VkCommandPool commandPool,
                            const void* data, VkDeviceSize size,
                            VkBufferUsageFlags dstUsage)
{
    Buffer staging = create(gpu, size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void* mapped;
    vkMapMemory(gpu.device(), staging.memory, 0, size, 0, &mapped);
    memcpy(mapped, data, static_cast<size_t>(size));
    vkUnmapMemory(gpu.device(), staging.memory);

    Buffer dst = create(gpu, size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | dstUsage,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandPool        = commandPool;
    ai.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(gpu.device(), &ai, &cmd);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    VkBufferCopy region{ 0, 0, size };
    vkCmdCopyBuffer(cmd, staging.buffer, dst.buffer, 1, &region);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    vkQueueSubmit(gpu.graphicsQueue(), 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(gpu.graphicsQueue());

    vkFreeCommandBuffers(gpu.device(), commandPool, 1, &cmd);
    staging.destroy(gpu.device());
    return dst;
}

Buffer Buffer::uploadStagedDeferred(GpuDevice& gpu, BufferUploadBatch& batch,
                                    const void* data, VkDeviceSize size,
                                    VkBufferUsageFlags dstUsage)
{
    Buffer staging = create(gpu, size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void* mapped;
    vkMapMemory(gpu.device(), staging.memory, 0, size, 0, &mapped);
    memcpy(mapped, data, static_cast<size_t>(size));
    vkUnmapMemory(gpu.device(), staging.memory);

    Buffer dst = create(gpu, size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | dstUsage,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // Record the copy into the shared command buffer; the caller submits it once
    // for the whole batch and waits a single fence (vs. a per-buffer queue drain).
    VkBufferCopy region{ 0, 0, size };
    vkCmdCopyBuffer(batch.cmd, staging.buffer, dst.buffer, 1, &region);

    batch.staging.push_back(std::move(staging));
    return dst;
}

Buffer Buffer::create(GpuDevice& gpu, VkDeviceSize size,
                      VkBufferUsageFlags usage, VkMemoryPropertyFlags props)
{
    VkBufferCreateInfo ci{};
    ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size        = size;
    ci.usage       = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buf;
    if (vkCreateBuffer(gpu.device(), &ci, nullptr, &buf) != VK_SUCCESS)
        throw std::runtime_error("Failed to create buffer");

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(gpu.device(), buf, &req);

    VkMemoryAllocateInfo mai{};
    mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize  = req.size;
    mai.memoryTypeIndex = findMemoryType(gpu.physicalDevice(), req.memoryTypeBits, props);

    VkDeviceMemory mem;
    if (vkAllocateMemory(gpu.device(), &mai, nullptr, &mem) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate buffer memory");

    vkBindBufferMemory(gpu.device(), buf, mem, 0);
    return Buffer(buf, mem, size);
}

uint32_t Buffer::findMemoryType(VkPhysicalDevice pd, uint32_t filter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(pd, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        if ((filter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("Failed to find suitable memory type");
}

Buffer::Buffer(Buffer&& o) noexcept
    : buffer(o.buffer), memory(o.memory), size(o.size)
{
    o.buffer = VK_NULL_HANDLE;
    o.memory = VK_NULL_HANDLE;
    o.size   = 0;
}

Buffer& Buffer::operator=(Buffer&& o) noexcept {
    buffer   = o.buffer;  memory   = o.memory;  size   = o.size;
    o.buffer = VK_NULL_HANDLE; o.memory = VK_NULL_HANDLE; o.size = 0;
    return *this;
}

void Buffer::destroy(VkDevice device) {
    if (buffer != VK_NULL_HANDLE) { vkDestroyBuffer(device, buffer, nullptr); buffer = VK_NULL_HANDLE; }
    if (memory != VK_NULL_HANDLE) { vkFreeMemory(device,    memory, nullptr); memory = VK_NULL_HANDLE; }
    size = 0;
}

void Buffer::mapMemory(GpuDevice& gpu, void* mappedPointer) {
    vkMapMemory(gpu.device(), this->memory, 0, this->size, 0, &mappedPointer);
}
