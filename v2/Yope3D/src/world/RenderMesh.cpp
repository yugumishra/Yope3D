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

    // STORAGE_BUFFER on top of VERTEX_BUFFER so skin.comp can read this buffer as
    // an SSBO. Set unconditionally rather than only for skinned meshes: skin data
    // arrives after construction (World::attachSkin), and a usage flag on a
    // device-local buffer costs nothing.
    vertexBuffer = Buffer::uploadStaged(gpu, commandPool,
        packed.data(), sizeof(PackedVertex) * packed.size(),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    indexBuffer = Buffer::uploadStaged(gpu, commandPool,
        indices.data(), sizeof(uint32_t) * indices.size(),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    indexCount   = static_cast<uint32_t>(indices.size());
    vertexCount_ = static_cast<uint32_t>(packed.size());
}

RenderMesh::RenderMesh(GpuDevice& gpu, BufferUploadBatch& batch,
                       const std::vector<Vertex>&   vertices,
                       const std::vector<uint32_t>& indices)
    : cpuVertices(vertices), cpuIndices(indices)
{
    const std::vector<PackedVertex> packed = meshbuild::buildPacked(vertices, indices);

    // See the blocking-upload constructor for why STORAGE is always set.
    vertexBuffer = Buffer::uploadStagedDeferred(gpu, batch,
        packed.data(), sizeof(PackedVertex) * packed.size(),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    indexBuffer = Buffer::uploadStagedDeferred(gpu, batch,
        indices.data(), sizeof(uint32_t) * indices.size(),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    indexCount   = static_cast<uint32_t>(indices.size());
    vertexCount_ = static_cast<uint32_t>(packed.size());
}

void RenderMesh::destroy(VkDevice device) {
    // Hand the compute descriptor set back before the buffers it points at go
    // away. The pool is created with FREE_DESCRIPTOR_SET_BIT for exactly this.
    if (skinSet != VK_NULL_HANDLE && skinSetPool != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(device, skinSetPool, 1, &skinSet);
        skinSet     = VK_NULL_HANDLE;
        skinSetPool = VK_NULL_HANDLE;
    }
    skinnedVertexBuffer.destroy(device);
    skinBuffer.destroy(device);
    indexBuffer.destroy(device);
    vertexBuffer.destroy(device);
    indexCount     = 0;
    vertexCount_   = 0;
    influenceCount = 0;
}

void RenderMesh::draw(VkCommandBuffer cmd) const {
    // A skinned mesh binds the compute pass's OUTPUT buffer, which holds the same
    // 32-byte PackedVertex layout as the source. This one line is what lets the
    // main, shadow and picking passes stay completely unaware of skinning — they
    // all call draw() and get correctly deformed geometry for free.
    VkBuffer     buf    = isSkinned() ? skinnedVertexBuffer.get() : vertexBuffer.get();
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &buf, &offset);
    vkCmdBindIndexBuffer(cmd, indexBuffer.get(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
}
