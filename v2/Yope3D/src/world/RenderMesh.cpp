#include "RenderMesh.h"
#include "../gpu/GpuDevice.h"
#include "../rendering/MeshBuild.h"

RenderMesh::RenderMesh(GpuDevice& gpu, VkCommandPool commandPool,
                       const std::vector<Vertex>&   vertices,
                       const std::vector<uint32_t>& indices)
    : cpuVertices(vertices), cpuIndices(indices)
{
    // Derive a tangent frame and pack into the 32-byte octahedral GPU format.
    // cpuVertices keeps the plain float Vertex (positions feed the raytracer).
    const std::vector<PackedVertex> packed = meshbuild::buildPacked(vertices, indices);

    vertexBuffer = Buffer::uploadStaged(gpu, commandPool,
        packed.data(), sizeof(PackedVertex) * packed.size(),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    indexBuffer = Buffer::uploadStaged(gpu, commandPool,
        indices.data(), sizeof(uint32_t) * indices.size(),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    indexCount = static_cast<uint32_t>(indices.size());
}

RenderMesh::RenderMesh(GpuDevice& gpu, BufferUploadBatch& batch,
                       const std::vector<Vertex>&   vertices,
                       const std::vector<uint32_t>& indices)
    : cpuVertices(vertices), cpuIndices(indices)
{
    const std::vector<PackedVertex> packed = meshbuild::buildPacked(vertices, indices);

    vertexBuffer = Buffer::uploadStagedDeferred(gpu, batch,
        packed.data(), sizeof(PackedVertex) * packed.size(),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    indexBuffer = Buffer::uploadStagedDeferred(gpu, batch,
        indices.data(), sizeof(uint32_t) * indices.size(),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    indexCount = static_cast<uint32_t>(indices.size());
}

RenderMesh::RenderMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
    : cpuVertices(vertices), cpuIndices(indices)
{
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
