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

RenderMesh::RenderMesh(GpuDevice& gpu, DynamicTag,
                       uint32_t maxVertices, uint32_t maxIndices)
    : dynamic_(true), dynMaxVerts_(maxVertices), dynMaxIndices_(maxIndices)
{
    for (auto& ring : dynRing_)
        ring.init(gpu, maxVertices, sizeof(PackedVertex), maxIndices);

    // indexCount stays 0 until the first setDynamicGeometry, so draw() is a
    // no-op rather than reading whatever the fresh mapping happens to contain.
    indexCount   = 0;
    vertexCount_ = 0;
}

bool RenderMesh::setDynamicGeometry(const std::vector<Vertex>&   vertices,
                                    const std::vector<uint32_t>& indices)
{
    if (!dynamic_) return false;

    // Capacity + index-range precondition. Rejected wholesale so the mesh keeps
    // drawing its previous geometry — a clamped partial apply would draw a torn
    // mix of two frames. See meshbuild::validateGeometry for why the static path
    // needs none of this.
    if (!meshbuild::validateGeometry(vertices.size(), indices,
                                     dynMaxVerts_, dynMaxIndices_))
        return false;

    // THE expensive line — tangent derivation + octahedral pack over the whole
    // array, every update. See the COST note in RenderMesh.h before making this
    // run more often.
    dynStagedVerts_   = meshbuild::buildPacked(vertices, indices);
    dynStagedIndices_ = indices;

    // Retained for the same reasons the static path retains them: the raytracer
    // reads cpuVertices for triangle-soup intersection, and the collider baker
    // and inspector read them for geometry queries.
    cpuVertices = vertices;
    cpuIndices  = indices;

    indexCount   = static_cast<uint32_t>(indices.size());
    vertexCount_ = static_cast<uint32_t>(dynStagedVerts_.size());

    // Every slot now holds stale geometry.
    dynDirtyMask_ = (1u << kFramesInFlight) - 1u;
    return true;
}

void RenderMesh::uploadDynamic(uint32_t slot) {
    if (!dynamic_ || slot >= kFramesInFlight) return;

    // Bind this slot regardless — the ring rotates every frame even when the
    // geometry is unchanged, and a clean slot already holds the current data.
    drawSlot_ = slot;

    const uint32_t bit = 1u << slot;
    if (!(dynDirtyMask_ & bit)) return;

    dynRing_[slot].write(dynStagedVerts_.data(),
                         static_cast<uint32_t>(dynStagedVerts_.size()),
                         dynStagedIndices_.data(),
                         static_cast<uint32_t>(dynStagedIndices_.size()));
    dynDirtyMask_ &= ~bit;
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
    for (auto& ring : dynRing_) ring.destroy(device);
    indexCount     = 0;
    vertexCount_   = 0;
    influenceCount = 0;
}

void RenderMesh::draw(VkCommandBuffer cmd) const {
    // A skinned mesh binds the compute pass's OUTPUT buffer, which holds the same
    // 32-byte PackedVertex layout as the source. This one line is what lets the
    // main, shadow and picking passes stay completely unaware of skinning — they
    // all call draw() and get correctly deformed geometry for free.
    //
    // A dynamic mesh binds its current ring slot instead, which is what lets the
    // main, shadow and picking passes stay equally unaware of CPU-updated
    // geometry. Its index buffer lives in the same ring (topology may change
    // between updates), and indexCount comes from the slot rather than the mesh
    // so a clamped write draws less geometry instead of reading past the end.
    if (dynamic_) {
        const DynamicMeshBuffer& ring = dynRing_[drawSlot_];
        if (!ring.valid() || ring.indexCount() == 0) return;
        VkBuffer     dynBuf    = ring.vertexBuffer();
        VkDeviceSize dynOffset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &dynBuf, &dynOffset);
        vkCmdBindIndexBuffer(cmd, ring.indexBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, ring.indexCount(), 1, 0, 0, 0);
        return;
    }

    VkBuffer     buf    = isSkinned() ? skinnedVertexBuffer.get() : vertexBuffer.get();
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &buf, &offset);
    vkCmdBindIndexBuffer(cmd, indexBuffer.get(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
}
