#include "RenderMesh.h"
#include "../gpu/GpuDevice.h"

RenderMesh::RenderMesh(GpuDevice& gpu, VkCommandPool commandPool,
                       const std::vector<Vertex>&   vertices,
                       const std::vector<uint32_t>& indices)
    : cpuVertices(vertices), cpuIndices(indices)
{
    vertexBuffer = Buffer::uploadStaged(gpu, commandPool,
        vertices.data(), sizeof(Vertex) * vertices.size(),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    indexBuffer = Buffer::uploadStaged(gpu, commandPool,
        indices.data(), sizeof(uint32_t) * indices.size(),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    indexCount = static_cast<uint32_t>(indices.size());
}

void RenderMesh::destroy(VkDevice device) {
    indexBuffer.destroy(device);
    vertexBuffer.destroy(device);
    indexCount = 0;
}

void RenderMesh::draw(VkCommandBuffer cmd) const {
    VkBuffer     buf    = vertexBuffer.get();
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &buf, &offset);
    vkCmdBindIndexBuffer(cmd, indexBuffer.get(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
}
